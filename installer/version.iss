#define VersionFileHandle FileOpen(SourcePath + "VERSION")
#if !VersionFileHandle
  #error Cannot open the repository VERSION file
#endif
#define RepositoryVersion Trim(FileRead(VersionFileHandle))
#expr FileClose(VersionFileHandle)
#ifdef ProductVersion
  #if ProductVersion != RepositoryVersion
    #error ProductVersion must match the repository VERSION file
  #endif
#else
  #define ProductVersion RepositoryVersion
#endif
