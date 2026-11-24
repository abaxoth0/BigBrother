/**
 * @file version.h
 * @brief Require the repository VERSION value supplied by the build as BB_VERSION.
 */

#ifndef BB_VERSION_H
#define BB_VERSION_H

#ifndef BB_VERSION
#error Define BB_VERSION from the repository VERSION file when compiling
#endif

#endif
