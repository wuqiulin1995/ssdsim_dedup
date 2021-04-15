# ssdsim_dedup


1. initialize.c : initialize the structures of ssd, mappings, channels and so on, load parameters from the parameter file;
2. interface.c : get requests from the trace file, a request includes timestamp, start lpn, length, read/write flag, fingerprint;
3. buffer.c : no buffer simulation, just handle the acquired requests;
4. ftl.c : functional module of FTL, address allocation, GC and so on;
5. flash.c : flash time line simulation.
