# Soft warning if the built ROM is getting close to the GBA's hard 32MB cart
# ceiling (gba_cart.ld's MEMORY{ROM: LENGTH = 32M}). Not a hard failure -
# there's real headroom expected (~13MB at time of writing), so this only
# fires as a heads-up if that margin has eroded.
set(kWarnThresholdBytes 31457280) # 30MB
set(kHardLimitBytes 33554432)     # 32MB

if(NOT EXISTS "${ROM_FILE}")
    return()
endif()

file(SIZE "${ROM_FILE}" romSize)
math(EXPR romSizeMB "${romSize} / 1048576")

if(romSize GREATER kHardLimitBytes)
    message(WARNING "first.gba is ${romSizeMB}MB - OVER the GBA's 32MB cart ROM ceiling!")
elseif(romSize GREATER kWarnThresholdBytes)
    message(WARNING "first.gba is ${romSizeMB}MB - approaching the GBA's 32MB cart ROM ceiling.")
endif()
