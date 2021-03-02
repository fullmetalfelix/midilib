from ctypes import *
import sys


lib = CDLL("./lib/libmidilib.so")
lib.midi_binarize.argtypes = [c_char_p, c_char_p]
lib.midi_binarize.restype = c_int



class float3(Structure):
	_fields_ = [
		("x", c_float),
		("y", c_float),
		("z", c_float)
	]

	def __init__(self, arr):
		self.x = c_float(arr[0])
		self.y = c_float(arr[1])
		self.z = c_float(arr[2])

	def __str__(self):
		return "[{}, {}, {}]".format(self.x, self.y, self.z)

	def toArray(self):
		return [self.x, self.y, self.z]

float3_p = POINTER(float3)






class MIDI2BIN(Structure):

	_fields_ = []


	def __init__(self):

		self.lib = CDLL("libmidilib.so")



def MIDI_Binaryze(filein, fileout):

	fmidi = c_char_p(filein.encode("utf-8"))
	fout = c_char_p(fileout.encode("utf-8"))
	lib.midi_binarize(fmidi, fout) 


	return
