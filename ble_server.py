#!/usr/bin/env python3
"""
BLE GATT Server for Rover Weight Monitoring
Receives sensor data from C++ process via Unix socket
Broadcasts data to Flutter app via BLE
"""

import dbus
import dbus.exceptions
import dbus.mainloop.glib
import dbus.service
import socket
import struct
import threading
import time
import sys
from gi.repository import GLib

# ============================================
# Configuration - MUST MATCH YOUR FLUTTER APP
# ============================================
SOCKET_PATH = '/tmp/rover_sensor.sock'
SERVICE_UUID = '3f09d95b-7f10-4c6a-8f0d-15a74be2b9b5'
WEIGHT_CHAR_UUID = 'a18f1f42-1f7d-4f62-9b9c-57e76a4c3140'
EVENTS_CHAR_UUID = 'b3a1f6d4-37db-4e7c-a7ac-b3e74c3f8e6a'
DEVICE_NAME = 'Rover-01'
BEARING_CHAR_UUID = 'd4a2f8e1-5b7c-4a3d-9e2f-1b8c4d3e2a1b' # Use a unique one
GPS_WRITE_CHAR_UUID = 'e5b3c9f2-6d8e-4f1a-8c2d-2b9a1c3d4e5f' # For phone to Pi
UDP_IP = "127.0.0.1"
UDP_PORT = 5005

MOTOR_CONTROL_CHAR_UUID = 'c7d8e9f0-1a2b-3c4d-5e6f-7a8b9c0d1e2f' # Make sure Flutter uses this!
UDP_IP = "127.0.0.1"
UDP_PORT = 5005
UDP_MOTOR_PORT = 5006

# ============================================
# BlueZ D-Bus Constants
# ============================================
BLUEZ_SERVICE_NAME = 'org.bluez'
GATT_MANAGER_IFACE = 'org.bluez.GattManager1'
LE_ADVERTISING_MANAGER_IFACE = 'org.bluez.LEAdvertisingManager1'
DBUS_OM_IFACE = 'org.freedesktop.DBus.ObjectManager'
DBUS_PROP_IFACE = 'org.freedesktop.DBus.Properties'
GATT_SERVICE_IFACE = 'org.bluez.GattService1'
GATT_CHRC_IFACE = 'org.bluez.GattCharacteristic1'
LE_ADVERTISEMENT_IFACE = 'org.bluez.LEAdvertisement1'

# ============================================
# Shared Sensor Data (Updated by Socket Reader)
# ============================================
class SensorData:
    def __init__(self):
        self.weight_bytes = [0x00, 0x00, 0x00, 0x00]  # 4 bytes (float)
        self.events_bytes = [0x00, 0x00]              # 2 bytes (uint16)
        self.bearing_bytes = [0x00, 0x00, 0x00, 0x00]
        self.lock = threading.Lock()
    
    def update_from_hex(self, weight_hex, events_hex, bearing_hex):
        """Update from hex strings received from C++"""
        with self.lock:
            # Parse weight bytes (4 bytes)
            self.weight_bytes = [
                int(weight_hex[0:2], 16),
                int(weight_hex[2:4], 16),
                int(weight_hex[4:6], 16),
                int(weight_hex[6:8], 16)
            ]
            # Parse events bytes (2 bytes)
            self.events_bytes = [
                int(events_hex[0:2], 16),
                int(events_hex[2:4], 16)
            ]
            if bearing_hex and len(bearing_hex) == 8:
                self.bearing_bytes = [int(bearing_hex[i:i+2], 16) for i in range(0, 8, 2)]

    def get_bearing_bytes(self):
        with self.lock:
            return self.bearing_bytes.copy()
    
    def get_weight_bytes(self):
        with self.lock:
            return self.weight_bytes.copy()
    
    def get_events_bytes(self):
        with self.lock:
            return self.events_bytes.copy()
    
    def get_weight_float(self):
        with self.lock:
            return struct.unpack('<f', bytes(self.weight_bytes))[0]
    
    def get_events_int(self):
        with self.lock:
            return struct.unpack('<H', bytes(self.events_bytes))[0]

sensor_data = SensorData()

# Global references to characteristics (set after creation)
weight_characteristic = None
events_characteristic = None
mainloop = None

# ============================================
# Socket Reader Thread
# ============================================
def socket_reader_thread():
    """Connect to C++ process and receive sensor data"""
    global weight_characteristic, events_characteristic
    
    while True:
        try:
            print('[Socket] Connecting to C++ sensor publisher...')
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect(SOCKET_PATH)
            print('[Socket] Connected to C++ process!')
            
            buffer = ''
            while True:
                data = sock.recv(1024)
                if not data:
                    print('[Socket] Connection closed by C++')
                    break
                
                buffer += data.decode('utf-8')
                
                # Process complete lines
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    if line:
                        parse_sensor_line(line)
        
        except FileNotFoundError:
            print('[Socket] C++ process not running. Start it with: ./sensor_publisher')
            print('[Socket] Retrying in 3 seconds...')
        except ConnectionRefusedError:
            print('[Socket] Connection refused. Is C++ process running?')
            print('[Socket] Retrying in 3 seconds...')
        except Exception as e:
            print(f'[Socket] Error: {e}')
            print('[Socket] Retrying in 3 seconds...')
        
        time.sleep(3)

def parse_sensor_line(line):
    """Parse data line from C++: W:0000803F,E:0000,B:XXXXXXXX"""
    global weight_characteristic, events_characteristic, bearing_characteristic
    
    try:
        # Parse format: "W:XXXXXXXX,E:XXXX"
        parts = line.split(',')
        weight_hex = None
        events_hex = None
        bearing_hex = None

        for part in parts:
            if part.startswith('W:'):
                weight_hex = part[2:]
            elif part.startswith('E:'):
                events_hex = part[2:]
            elif part.startswith('B:'): 
                bearing_hex = part[2:]
        
        if (weight_hex and events_hex and len(weight_hex) == 8 and len(events_hex) == 4 and \
            (bearing_hex is None or len(bearing_hex) == 8)):
            sensor_data.update_from_hex(weight_hex, events_hex, bearing_hex)
            
            # Schedule notifications on main thread
            GLib.idle_add(send_notifications)
        else:
            print(f'[Parse] Invalid format: {line}')
    
    except Exception as e:
        print(f'[Parse] Error parsing "{line}": {e}')

def send_notifications():
    """Send BLE notifications (called from main GLib thread)"""
    global weight_characteristic, events_characteristic, bearing_characteristic
    
    try:
        if weight_characteristic and weight_characteristic.notifying:
            weight_characteristic.send_notification()
        
        if events_characteristic and events_characteristic.notifying:
            events_characteristic.send_notification()

        if bearing_characteristic and bearing_characteristic.notifying:
            bearing_characteristic.send_notification()
    except Exception as e:
        print(f'[Notify] Error: {e}')
    
    return False  # Don't repeat (one-shot idle callback)

# ============================================
# D-Bus Exceptions
# ============================================
class InvalidArgsException(dbus.exceptions.DBusException):
    _dbus_error_name = 'org.freedesktop.DBus.Error.InvalidArgs'

class NotSupportedException(dbus.exceptions.DBusException):
    _dbus_error_name = 'org.bluez.Error.NotSupported'

class NotPermittedException(dbus.exceptions.DBusException):
    _dbus_error_name = 'org.bluez.Error.NotPermitted'

# ============================================
# GATT Application
# ============================================
class Application(dbus.service.Object):
    """
    org.bluez.GattApplication1 interface implementation
    """
    def __init__(self, bus):
        self.path = '/'
        self.services = []
        dbus.service.Object.__init__(self, bus, self.path)
        self.add_service(RoverService(bus, 0))

    def get_path(self):
        return dbus.ObjectPath(self.path)

    def add_service(self, service):
        self.services.append(service)

    @dbus.service.method(DBUS_OM_IFACE, out_signature='a{oa{sa{sv}}}')
    def GetManagedObjects(self):
        response = {}
        print('[App] GetManagedObjects called')
        
        for service in self.services:
            response[service.get_path()] = service.get_properties()
            chrcs = service.get_characteristics()
            for chrc in chrcs:
                response[chrc.get_path()] = chrc.get_properties()
                descs = chrc.get_descriptors()
                for desc in descs:
                    response[desc.get_path()] = desc.get_properties()
        
        return response

# ============================================
# GATT Service Base Class
# ============================================
class Service(dbus.service.Object):
    """
    org.bluez.GattService1 interface implementation
    """
    PATH_BASE = '/org/bluez/example/service'

    def __init__(self, bus, index, uuid, primary):
        self.path = self.PATH_BASE + str(index)
        self.bus = bus
        self.uuid = uuid
        self.primary = primary
        self.characteristics = []
        dbus.service.Object.__init__(self, bus, self.path)

    def get_properties(self):
        return {
            GATT_SERVICE_IFACE: {
                'UUID': self.uuid,
                'Primary': self.primary,
                'Characteristics': dbus.Array(
                    self.get_characteristic_paths(),
                    signature='o')
            }
        }

    def get_path(self):
        return dbus.ObjectPath(self.path)

    def add_characteristic(self, characteristic):
        self.characteristics.append(characteristic)

    def get_characteristic_paths(self):
        return [chrc.get_path() for chrc in self.characteristics]

    def get_characteristics(self):
        return self.characteristics

    @dbus.service.method(DBUS_PROP_IFACE,
                         in_signature='s',
                         out_signature='a{sv}')
    def GetAll(self, interface):
        if interface != GATT_SERVICE_IFACE:
            raise InvalidArgsException()
        return self.get_properties()[GATT_SERVICE_IFACE]

# ============================================
# GATT Characteristic Base Class
# ============================================
class Characteristic(dbus.service.Object):
    """
    org.bluez.GattCharacteristic1 interface implementation
    """
    def __init__(self, bus, index, uuid, flags, service):
        self.path = service.path + '/char' + str(index)
        self.bus = bus
        self.uuid = uuid
        self.service = service
        self.flags = flags
        self.descriptors = []
        self.notifying = False
        dbus.service.Object.__init__(self, bus, self.path)

    def get_properties(self):
        return {
            GATT_CHRC_IFACE: {
                'Service': self.service.get_path(),
                'UUID': self.uuid,
                'Flags': self.flags,
                'Descriptors': dbus.Array(
                    self.get_descriptor_paths(),
                    signature='o')
            }
        }

    def get_path(self):
        return dbus.ObjectPath(self.path)

    def add_descriptor(self, descriptor):
        self.descriptors.append(descriptor)

    def get_descriptor_paths(self):
        return [desc.get_path() for desc in self.descriptors]

    def get_descriptors(self):
        return self.descriptors

    @dbus.service.method(DBUS_PROP_IFACE,
                         in_signature='s',
                         out_signature='a{sv}')
    def GetAll(self, interface):
        if interface != GATT_CHRC_IFACE:
            raise InvalidArgsException()
        return self.get_properties()[GATT_CHRC_IFACE]

    @dbus.service.method(GATT_CHRC_IFACE,
                         in_signature='a{sv}',
                         out_signature='ay')
    def ReadValue(self, options):
        print(f'[Char] Default ReadValue called on {self.uuid}')
        raise NotSupportedException()

    @dbus.service.method(GATT_CHRC_IFACE, in_signature='aya{sv}')
    def WriteValue(self, value, options):
        print(f'[Char] Default WriteValue called on {self.uuid}')
        raise NotSupportedException()

    @dbus.service.method(GATT_CHRC_IFACE)
    def StartNotify(self):
        if self.notifying:
            print(f'[Char] Already notifying on {self.uuid}')
            return
        self.notifying = True
        print(f'[Char] StartNotify on {self.uuid}')

    @dbus.service.method(GATT_CHRC_IFACE)
    def StopNotify(self):
        if not self.notifying:
            print(f'[Char] Not notifying on {self.uuid}')
            return
        self.notifying = False
        print(f'[Char] StopNotify on {self.uuid}')

    @dbus.service.signal(DBUS_PROP_IFACE,
                         signature='sa{sv}as')
    def PropertiesChanged(self, interface, changed, invalidated):
        pass

    def send_notification(self):
        """Override in subclasses to send actual data"""
        pass

# ============================================
# GATT Descriptor Base Class
# ============================================
class Descriptor(dbus.service.Object):
    """
    org.bluez.GattDescriptor1 interface implementation
    """
    def __init__(self, bus, index, uuid, flags, characteristic):
        self.path = characteristic.path + '/desc' + str(index)
        self.bus = bus
        self.uuid = uuid
        self.flags = flags
        self.chrc = characteristic
        dbus.service.Object.__init__(self, bus, self.path)

    def get_properties(self):
        return {
            'org.bluez.GattDescriptor1': {
                'Characteristic': self.chrc.get_path(),
                'UUID': self.uuid,
                'Flags': self.flags,
            }
        }

    def get_path(self):
        return dbus.ObjectPath(self.path)

    @dbus.service.method(DBUS_PROP_IFACE,
                         in_signature='s',
                         out_signature='a{sv}')
    def GetAll(self, interface):
        if interface != 'org.bluez.GattDescriptor1':
            raise InvalidArgsException()
        return self.get_properties()['org.bluez.GattDescriptor1']

    @dbus.service.method('org.bluez.GattDescriptor1',
                         in_signature='a{sv}',
                         out_signature='ay')
    def ReadValue(self, options):
        print(f'[Desc] Default ReadValue called')
        raise NotSupportedException()

    @dbus.service.method('org.bluez.GattDescriptor1',
                         in_signature='aya{sv}')
    def WriteValue(self, value, options):
        print(f'[Desc] Default WriteValue called')
        raise NotSupportedException()

# ============================================
# Rover Service Implementation
# ============================================
class RoverService(Service):
    """
    Custom service for Rover weight monitoring
    """
    def __init__(self, bus, index):
        Service.__init__(self, bus, index, SERVICE_UUID, True)
        
        # Create characteristics
        global weight_characteristic, events_characteristic, bearing_characteristic
        
        weight_characteristic = WeightCharacteristic(bus, 0, self)
        events_characteristic = EventsCharacteristic(bus, 1, self)
        bearing_characteristic = BearingCharacteristic(bus, 2, self)
        gps_write_characteristic = GPSWriteCharacteristic(bus, 3, self)

        motor_control_characteristic = MotorControlCharacteristic(bus, 4, self)
        
        self.add_characteristic(weight_characteristic)
        self.add_characteristic(events_characteristic)
        self.add_characteristic(bearing_characteristic)     
        self.add_characteristic(gps_write_characteristic)

        self.add_characteristic(motor_control_characteristic)
        
        print(f'[Service] Created Rover service: {SERVICE_UUID}')

# ============================================
# Weight Characteristic Implementation
# ============================================
class WeightCharacteristic(Characteristic):
    """
    Weight characteristic - sends weight data as 4-byte float (little-endian)
    """
    def __init__(self, bus, index, service):
        Characteristic.__init__(
            self, bus, index,
            WEIGHT_CHAR_UUID,
            ['read', 'notify'],
            service)
        
        # Add user description descriptor
        self.add_descriptor(
            CharacteristicUserDescriptionDescriptor(bus, 0, self, 'Weight (lb)'))
        
        print(f'[Char] Created Weight characteristic: {WEIGHT_CHAR_UUID}')

    def ReadValue(self, options):
        value = sensor_data.get_weight_bytes()
        weight = sensor_data.get_weight_float()
        print(f'[Weight] ReadValue: {weight:.2f} lb -> {value}')
        return dbus.Array(value, signature='y')

    def send_notification(self):
        if not self.notifying:
            return
        
        value = sensor_data.get_weight_bytes()
        weight = sensor_data.get_weight_float()
        
        self.PropertiesChanged(
            GATT_CHRC_IFACE,
            {'Value': dbus.Array(value, signature='y')},
            [])
        
        print(f'[Weight] Notification: {weight:.2f} lb')

# ============================================
# Events Characteristic Implementation
# ============================================
class EventsCharacteristic(Characteristic):
    """
    Events characteristic - sends event flags as 2-byte uint16 (little-endian)
    """
    def __init__(self, bus, index, service):
        Characteristic.__init__(
            self, bus, index,
            EVENTS_CHAR_UUID,
            ['read', 'notify'],
            service)
        
        # Add user description descriptor
        self.add_descriptor(
            CharacteristicUserDescriptionDescriptor(bus, 1, self, 'Event Flags'))
        
        print(f'[Char] Created Events characteristic: {EVENTS_CHAR_UUID}')

    def ReadValue(self, options):
        value = sensor_data.get_events_bytes()
        events = sensor_data.get_events_int()
        print(f'[Events] ReadValue: 0x{events:04X} -> {value}')
        return dbus.Array(value, signature='y')

    def send_notification(self):
        if not self.notifying:
            return
        
        value = sensor_data.get_events_bytes()
        events = sensor_data.get_events_int()
        
        self.PropertiesChanged(
            GATT_CHRC_IFACE,
            {'Value': dbus.Array(value, signature='y')},
            [])
        
        print(f'[Events] Notification: 0x{events:04X}')





# ============================================
# Bearing Characteristic Implementation
# ============================================
class BearingCharacteristic(Characteristic):
    def __init__(self, bus, index, service):
        Characteristic.__init__(
            self, bus, index,
            BEARING_CHAR_UUID,
            ['read', 'notify'],
            service)
        self.add_descriptor(CharacteristicUserDescriptionDescriptor(bus, 2, self, 'Bearing (deg)'))

    def ReadValue(self, options):
        value = sensor_data.get_bearing_bytes()
        return dbus.Array(value, signature='y')

    def send_notification(self):
        if not self.notifying:
            return
        value = sensor_data.get_bearing_bytes()
        self.PropertiesChanged(
            GATT_CHRC_IFACE,
            {'Value': dbus.Array(value, signature='y')},
            [])

# ============================================
# GPS Write Characteristic Implementation
# ============================================
class GPSWriteCharacteristic(Characteristic):
    def __init__(self, bus, index, service):
        Characteristic.__init__(
            self, bus, index,
            GPS_WRITE_CHAR_UUID,
            ['write', 'write-without-response'],
            service)
        self.add_descriptor(CharacteristicUserDescriptionDescriptor(bus, 3, self, 'Phone GPS Input'))
        # Socket for sending to C++
        self.udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def WriteValue(self, value, options):
        try:
            raw_bytes = bytes(value)

            if len(raw_bytes) != 8:
                print(f"[GPS Write] Expected 8 bytes, got {len(raw_bytes)}")
                return
            lat, lon = struct.unpack('<ff', raw_bytes)
            print(f"[GPS Write] Received from Phone: Lat={lat:.6f}, Lon={lon:.6f}")
            # Convert dbus array of bytes to string
            gps_str = f"{lat:.6f},{lon:.6f}"
            #gps_str = "".join([chr(b) for b in value])
            # Send to C++ via UDP port 5005
            self.udp_sock.sendto(gps_str.encode(), (UDP_IP, UDP_PORT))
        except Exception as e:
            print(f'[GPS Write] Error: {e}')






# ============================================
# Motor Control Characteristic Implementation
# ============================================
class MotorControlCharacteristic(Characteristic):
    def __init__(self, bus, index, service):
        Characteristic.__init__(
            self, bus, index,
            MOTOR_CONTROL_CHAR_UUID,
            ['write', 'write-without-response'],
            service)
        self.add_descriptor(CharacteristicUserDescriptionDescriptor(bus, index, self, 'D-Pad Motor Control'))
        
        # Socket for sending to C++
        self.udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def WriteValue(self, value, options):
        try:
            # Convert dbus array of bytes to a standard string
            raw_bytes = bytes(value)
            command_str = raw_bytes.decode('utf-8').strip()
            
            print(f"[Motor Control] Received command from phone: {command_str}")
            
            # Send to C++ via UDP port 5006
            self.udp_sock.sendto(command_str.encode(), (UDP_IP, UDP_MOTOR_PORT))
            
        except Exception as e:
            print(f'[Motor Control] Error: {e}')





# ============================================
# Characteristic User Description Descriptor
# ============================================
class CharacteristicUserDescriptionDescriptor(Descriptor):
    """
    Characteristic User Description Descriptor (UUID 0x2901)
    """
    CUD_UUID = '2901'

    def __init__(self, bus, index, characteristic, description):
        self.description = description
        Descriptor.__init__(
            self, bus, index,
            self.CUD_UUID,
            ['read'],
            characteristic)

    def ReadValue(self, options):
        value = []
        for c in self.description:
            value.append(dbus.Byte(c.encode()))
        return value

# ============================================
# BLE Advertisement
# ============================================
class Advertisement(dbus.service.Object):
    """
    org.bluez.LEAdvertisement1 interface implementation
    """
    PATH_BASE = '/org/bluez/example/advertisement'

    def __init__(self, bus, index, advertising_type):
        self.path = self.PATH_BASE + str(index)
        self.bus = bus
        self.ad_type = advertising_type
        self.service_uuids = None
        self.manufacturer_data = None
        self.solicit_uuids = None
        self.service_data = None
        self.local_name = None
        self.include_tx_power = None
        dbus.service.Object.__init__(self, bus, self.path)

    def get_properties(self):
        properties = dict()
        properties['Type'] = self.ad_type
        
        if self.service_uuids is not None:
            properties['ServiceUUIDs'] = dbus.Array(self.service_uuids, signature='s')
        
        if self.solicit_uuids is not None:
            properties['SolicitUUIDs'] = dbus.Array(self.solicit_uuids, signature='s')
        
        if self.manufacturer_data is not None:
            properties['ManufacturerData'] = dbus.Dictionary(
                self.manufacturer_data, signature='qv')
        
        if self.service_data is not None:
            properties['ServiceData'] = dbus.Dictionary(
                self.service_data, signature='sv')
        
        if self.local_name is not None:
            properties['LocalName'] = dbus.String(self.local_name)
        
        if self.include_tx_power is not None:
            properties['Includes'] = dbus.Array(['tx-power'], signature='s')
        
        return {LE_ADVERTISEMENT_IFACE: properties}

    def get_path(self):
        return dbus.ObjectPath(self.path)

    def add_service_uuid(self, uuid):
        if not self.service_uuids:
            self.service_uuids = []
        self.service_uuids.append(uuid)

    def add_solicit_uuid(self, uuid):
        if not self.solicit_uuids:
            self.solicit_uuids = []
        self.solicit_uuids.append(uuid)

    def add_manufacturer_data(self, manuf_code, data):
        if not self.manufacturer_data:
            self.manufacturer_data = dbus.Dictionary({}, signature='qv')
        self.manufacturer_data[manuf_code] = dbus.Array(data, signature='y')

    def add_service_data(self, uuid, data):
        if not self.service_data:
            self.service_data = dbus.Dictionary({}, signature='sv')
        self.service_data[uuid] = dbus.Array(data, signature='y')

    def add_local_name(self, name):
        self.local_name = name

    def add_include_tx_power(self):
        self.include_tx_power = True

    @dbus.service.method(DBUS_PROP_IFACE,
                         in_signature='s',
                         out_signature='a{sv}')
    def GetAll(self, interface):
        print(f'[Adv] GetAll({interface})')
        if interface != LE_ADVERTISEMENT_IFACE:
            raise InvalidArgsException()
        return self.get_properties()[LE_ADVERTISEMENT_IFACE]

    @dbus.service.method(LE_ADVERTISEMENT_IFACE,
                         in_signature='',
                         out_signature='')
    def Release(self):
        print(f'[Adv] Released: {self.path}')

# ============================================
# Rover Advertisement
# ============================================
class RoverAdvertisement(Advertisement):
    """
    Advertisement for Rover device
    """
    def __init__(self, bus, index):
        Advertisement.__init__(self, bus, index, 'peripheral')
        self.add_service_uuid(SERVICE_UUID)
        self.add_local_name(DEVICE_NAME)
        self.add_include_tx_power()
        print(f'[Adv] Created Rover advertisement: {DEVICE_NAME}')

# ============================================
# Helper Functions
# ============================================
def find_adapter(bus):
    """
    Find the first Bluetooth adapter that supports GATT Manager and LE Advertising
    """
    remote_om = dbus.Interface(bus.get_object(BLUEZ_SERVICE_NAME, '/'),
                               DBUS_OM_IFACE)
    objects = remote_om.GetManagedObjects()

    for o, props in objects.items():
        if GATT_MANAGER_IFACE in props.keys():
            if LE_ADVERTISING_MANAGER_IFACE in props.keys():
                return o
    
    return None

def register_app_cb():
    """Callback for successful GATT application registration"""
    print('[BLE] GATT application registered successfully')

def register_app_error_cb(error):
    """Callback for failed GATT application registration"""
    print(f'[BLE] Failed to register application: {error}')
    mainloop.quit()

def register_ad_cb():
    """Callback for successful advertisement registration"""
    print('[BLE] Advertisement registered successfully')

def register_ad_error_cb(error):
    """Callback for failed advertisement registration"""
    print(f'[BLE] Failed to register advertisement: {error}')
    mainloop.quit()

def print_banner():
    """Print startup banner"""
    print('')
    print('╔═══════════════════════════════════════════════════════╗')
    print('║     Python BLE GATT Server for Rover                  ║')
    print('║     Receives data from C++ sensor publisher           ║')
    print('╚═══════════════════════════════════════════════════════╝')
    print('')

def print_config():
    """Print configuration"""
    print('Configuration:')
    print(f'├─ Socket Path:     {SOCKET_PATH}')
    print(f'├─ Device Name:     {DEVICE_NAME}')
    print(f'├─ Service UUID:    {SERVICE_UUID}')
    print(f'├─ Weight Char:     {WEIGHT_CHAR_UUID}')
    print(f'└─ Events Char:     {EVENTS_CHAR_UUID}')
    print('')

# ============================================
# Main Function
# ============================================
def main():
    global mainloop
    
    print_banner()
    print_config()
    
    # Initialize D-Bus
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    
    # Get system bus
    bus = dbus.SystemBus()
    
    # Find Bluetooth adapter
    adapter = find_adapter(bus)
    if not adapter:
        print('[BLE] ERROR: No Bluetooth adapter found!')
        print('[BLE] Make sure Bluetooth is enabled:')
        print('      sudo hciconfig hci0 up')
        print('      sudo systemctl start bluetooth')
        return 1
    
    print(f'[BLE] Using adapter: {adapter}')
    
    # Get adapter properties to show address
    try:
        adapter_props = dbus.Interface(
            bus.get_object(BLUEZ_SERVICE_NAME, adapter),
            DBUS_PROP_IFACE)
        adapter_address = adapter_props.Get('org.bluez.Adapter1', 'Address')
        print(f'[BLE] Adapter address: {adapter_address}')
    except Exception as e:
        print(f'[BLE] Could not get adapter address: {e}')
    
    # Get service manager interface
    service_manager = dbus.Interface(
        bus.get_object(BLUEZ_SERVICE_NAME, adapter),
        GATT_MANAGER_IFACE)
    
    # Get advertising manager interface
    ad_manager = dbus.Interface(
        bus.get_object(BLUEZ_SERVICE_NAME, adapter),
        LE_ADVERTISING_MANAGER_IFACE)
    
    # Create GATT application
    print('[BLE] Creating GATT application...')
    app = Application(bus)
    
    # Create advertisement
    print('[BLE] Creating advertisement...')
    adv = RoverAdvertisement(bus, 0)
    
    # Create main loop
    mainloop = GLib.MainLoop()
    
    # Register GATT application
    print('[BLE] Registering GATT application...')
    service_manager.RegisterApplication(
        app.get_path(), {},
        reply_handler=register_app_cb,
        error_handler=register_app_error_cb)
    
    # Register advertisement
    print('[BLE] Registering advertisement...')
    ad_manager.RegisterAdvertisement(
        adv.get_path(), {},
        reply_handler=register_ad_cb,
        error_handler=register_ad_error_cb)
    
    # Start socket reader thread (connects to C++ process)
    print('[BLE] Starting socket reader thread...')
    socket_thread = threading.Thread(target=socket_reader_thread, daemon=True)
    socket_thread.start()
    
    # Print ready message
    print('')
    print('╔═══════════════════════════════════════════════════════╗')
    print('║  BLE SERVER RUNNING                                   ║')
    print('║                                                       ║')
    print(f'║  Device Name: {DEVICE_NAME:<40} ║')
    print('║                                                       ║')
    print('║  Your Flutter app should see this device!             ║')
    print('║                                                       ║')
    print('║  Make sure C++ sensor_publisher is running:           ║')
    print('║    ./sensor_publisher                                 ║')
    print('║                                                       ║')
    print('║  Press Ctrl+C to stop                                 ║')
    print('╚═══════════════════════════════════════════════════════╝')
    print('')
    
    # Run main loop
    try:
        mainloop.run()
    except KeyboardInterrupt:
        print('\n[BLE] Shutting down...')
    
    # Cleanup
    print('[BLE] Unregistering advertisement...')
    try:
        ad_manager.UnregisterAdvertisement(adv.get_path())
    except Exception as e:
        print(f'[BLE] Error unregistering advertisement: {e}')
    
    print('[BLE] Unregistering application...')
    try:
        service_manager.UnregisterApplication(app.get_path())
    except Exception as e:
        print(f'[BLE] Error unregistering application: {e}')
    
    print('[BLE] Goodbye!')
    return 0

# ============================================
# Entry Point
# ============================================
if __name__ == '__main__':
    sys.exit(main())