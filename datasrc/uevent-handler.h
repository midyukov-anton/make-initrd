#ifndef _UEVENT_HANDLER_H_
#define _UEVENT_HANDLER_H_

#include <unistd.h>

pid_t spawn_handler(char *path, char **argv);

#endif /* _UEVENT_HANDLER_H_ */
