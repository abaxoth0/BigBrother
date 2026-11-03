#ifndef BuildJobs
  #define BuildJobs Max(1, Int(GetEnv("NUMBER_OF_PROCESSORS")))
#else
  #define BuildJobs Int(BuildJobs)
#endif

Compression=lzma2
SolidCompression=yes
CompressionThreads={#Min(2, BuildJobs)}
LZMANumBlockThreads={#Min(256, (BuildJobs + 1) / 2)}
