# Check SWIG Python bindings for LALPulsar
# Author: Karl Wette, 2011--2014

# check module load
print("checking module load ...")
import lal
import lalpulsar
from lalpulsar import globalvar as lalpulsarglobalvar
from lal import globalvar as lalglobalvar
print("PASSED module load")

# check object parent tracking
print("checking object parent tracking ...")
a = lalpulsar.swig_lalpulsar_test_parent_map_struct()
for i in range(0, 7):
    b = a.s
    c = lalpulsarglobalvar.swig_lalpulsar_test_parent_map.s
    lalpulsarglobalvar.swig_lalpulsar_test_parent_map.s = lalglobalvar.swig_lal_test_struct_const
del c
del b
del a
lal.CheckMemoryLeaks()
print("PASSED object parent tracking")

# check array element assignment
print("checking array element assignment ...")
mts = lalpulsar.CreateMultiLIGOTimeGPSVector(2)
ts0 = lalpulsar.CreateTimestampVector(3)
for i in range(ts0.length):
    ts0.data[i] = lal.LIGOTimeGPS(900000000 + i)
mts.data[0] = ts0
lal.swig_set_nasty_error_handlers()
del mts
del ts0
lal.swig_set_nice_error_handlers()
lal.CheckMemoryLeaks()
print("PASSED array element assignment")

# passed all tests!
print("PASSED all tests")
