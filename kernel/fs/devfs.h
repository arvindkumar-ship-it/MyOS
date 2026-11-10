#pragma once

// Device files under /dev:
//   /dev/console  write: screen + serial, read: keyboard/serial input
//   /dev/null     write: thrown away, read: always end of file
void devfs_init(void);
