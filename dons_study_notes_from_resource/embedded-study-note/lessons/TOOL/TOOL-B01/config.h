/* config.h — build-time configuration: macros only, no code, no storage */
#ifndef CONFIG_H
#define CONFIG_H

#define FW_MAJOR      2
#define FW_MINOR      7
#define SECTOR_SIZE   512u
#define LOG_ENABLED   1

#define STR_(x)       #x
#define STR(x)        STR_(x)
#define FW_VERSION    STR(FW_MAJOR) "." STR(FW_MINOR)       /* -> "2" "." "7" */

#define SECTORS_FOR(bytes)  (((bytes) + SECTOR_SIZE - 1u) / SECTOR_SIZE)

#if LOG_ENABLED
#define LOG(msg)      puts("[fw] " msg)
#else
#define LOG(msg)      ((void)0)
#endif

#endif /* CONFIG_H */
