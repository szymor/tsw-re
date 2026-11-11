#ifndef TSW_REPLAY_H
#define TSW_REPLAY_H

#include <stdbool.h>
#include <stddef.h>

/* Run a deterministic text replay script.  Commands:
 *   load <save>, new, move <up|down|left|right>,
 *   expect <floor|x|y|life|attack|defense|gold|limit|yellow|red|blue> <value>,
 *   expect tile <floor> <index> <value>, save <path>, print
 */
bool replay_run(const char *data_dir, const char *script_path,
                char *err, size_t errsz);

#endif
