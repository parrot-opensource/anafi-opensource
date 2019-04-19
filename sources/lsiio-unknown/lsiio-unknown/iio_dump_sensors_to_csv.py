#!/usr/bin/env python3

import os
import sys
import configparser
import csv
import re
import struct

class Error(Exception):
    pass

class ParseError(Error):
    pass

class Channel:
    def __init__(self, name, dict):
        self.name = name
        if not 'index' in dict:
            raise ParseError("Missing index for channel %s" % (name,))
        self.index = int(dict['index'])
        self.size = 0
        self.signed = False
        self.bigendian = False
        self.storage = 0
        self.shift = 0
        self.offset = 0.0
        self.scale = 1.0
        try:
            self.parse_type(dict['type'])
            if 'offset' in dict:
                self.offset = float(dict['offset'])
            if 'scale' in dict:
                self.scale = float(dict['scale'])
        except (Error, ValueError) as e:
            raise ParseError("Failed to parse channel %s" % (name,)) from e

    def parse_type(self, type):
        match = re.fullmatch(r'(le|be):([us])(\d+)/(\d+)>>(\d+)', type)
        if match is None:
            raise ParseError("Invalid type string: %s" % (type,))
        gr = match.groups()
        self.bigendian = gr[0] == 'be'
        self.signed = gr[1] == 's'
        self.size = int(gr[2])
        self.storage = int(gr[3])
        self.shift = int(gr[4])
        # Sanity checks
        if self.storage == 0:
            raise ParseError('Storage size is zero')
        if self.storage < self.size:
            raise ParseError("Sample size (%d) is larger than storage size (%d)" % (self.size, self.storage))
        if self.shift > (self.storage - self.size):
            raise ParseError("Value would be shifted out by %d bits" % (self.shift - (self.storage - self.size)))
        if self.storage != 64 and \
           self.storage != 32 and \
           self.storage != 16 and \
           self.storage != 8:
               raise ParseError("Unsupported storage size (%d). Supported values are 8, 16, 32 and 64" % (self.storage,))

    def __str__(self):
        return '%d: \"%s\" %d/%d' % (self.index, self.name, self.size, self.storage)

class Sensor:
    def __init__(self, input):
        self.name = os.path.basename(input)
        self.datapath = input + ".dat"
        self.channels = {}
        try:
            self.process_config(input)
        except Error as e:
            raise ParseError("%s.ini: Failed to parse sensor metadata" % (input,)) from e

    def process_config(self, input):
        cfg = configparser.ConfigParser()
        cfg.read(input + '.ini')
        for name,dict in cfg.items():
            if name != cfg.default_section:
                c = Channel(name, dict)
                self.channels[c.index] = c

    def __str__(self):
        s = "%s: %d sensors, data in %s:\n" % (self.name, len(self.channels), self.datapath)
        for i in self.channels:
            c = self.channels[i]
            s += "  %s\n" % (c,)
        return s

class SensorDataReader:
    def __init__(self, sensor):
        self.done = False
        self.sensor = sensor
        self.file = open(sensor.datapath, 'rb')
        self.rowlen = 0
        for c in self.sensor.channels.values():
            self.rowlen += int(c.storage / 8)
        print("Row length: %d" % (self.rowlen,))

    def __iter__(self):
        return self

    def __next__(self):
        if self.done:
            raise StopIteration

        bytes = bytearray(self.rowlen)
        done = self.file.readinto(bytes)
        if done != self.rowlen:
            del(self.file)
            self.done = True
            raise StopIteration
        return self.expand_row(bytes)

    def expand_row(self, row):
        data = {}
        pos = 0
        for i in range(len(self.sensor.channels)):
            c = self.sensor.channels[i]
            if c.storage == 8:
                val = int(row[pos:pos+1])[0]
                pos += 1
            elif c.storage == 16:
                if c.bigendian:
                    val = struct.unpack('>H', row[pos:pos+2])[0]
                else:
                    val = struct.unpack('<H', row[pos:pos+2])[0]
                pos += 2
            elif c.storage == 32:
                if c.bigendian:
                    val = struct.unpack('>L', row[pos:pos+4])[0]
                else:
                    val = struct.unpack('<L', row[pos:pos+4])[0]
                pos += 4
            elif c.storage == 64:
                if c.bigendian:
                    val = struct.unpack('>Q', row[pos:pos+8])[0]
                else:
                    val = struct.unpack('<Q', row[pos:pos+8])[0]
                pos += 8
            else:
                raise ValueError('Storage size is invalid')
            val = val >> c.shift
            # Sign-extend
            if c.signed and val & (1 << (c.size - 1)):
                val |= ~((1 << c.size) - 1)
            else:
                val &= (1 << c.size) - 1
            data[c.name] = (float(val) +c.offset) * c.scale
        return data

    def __str__(self):
        return "SensorDataReader for %s" % (self.sensor.datapath,)


files = sys.argv[1:]
stems = []
sensors = []

for idx, file in enumerate(files):
    stems.append(file.rpartition('.')[0])
# Remove duplicates
stems = list(dict.fromkeys(stems))

for input in stems:
    print("Processing %s..." % (input,))
    s = Sensor(input)
    sensors.append(s)

for s in sensors:
    print(s)
    reader = SensorDataReader(s)
    print(reader)
    fieldnames = []
    for i in range(len(s.channels)):
        fieldnames.append(s.channels[i].name)

    with open(s.datapath.rpartition('.')[0] + '.csv', 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames)
        writer.writeheader()
        writer.writerows(reader)
