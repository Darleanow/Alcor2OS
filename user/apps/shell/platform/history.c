/**
 * @file platform/history.c
 * @brief Bounded history ring for the shell's line editor.
 *
 * Oldest entry at index 0; newest at @c hist_count-1. UP/DOWN in
 * ::sh_read_line navigate a transient cursor into this list while editing.
 */

#include <shell/shell.h>
#include <string.h>

#define HIST_MAX     128
#define LINE_MAX_LEN MAX_CMD_LEN

static char history[HIST_MAX][LINE_MAX_LEN];
static int  hist_count = 0;

/**
 * @brief Append @p line to history.
 *
 * No-op when @p line is empty or duplicates the most recent entry. Oldest
 * entry is dropped (shift-left) once the ring fills.
 *
 * @param line  Line to record (must outlive the call; copied into the ring).
 */
void sh_hist_push(const char *line)
{
  if(!line || !line[0])
    return;
  /* Skip if identical to the most recent entry. */
  if(hist_count > 0 && strcmp(history[hist_count - 1], line) == 0)
    return;
  if(hist_count == HIST_MAX) {
    (void)memmove(history[0], history[1], sizeof(history[0]) * (HIST_MAX - 1));
    hist_count--;
  }
  strncpy(history[hist_count], line, LINE_MAX_LEN - 1);
  history[hist_count][LINE_MAX_LEN - 1] = '\0';
  hist_count++;
}

/**
 * @brief Return the number of entries currently held.
 *
 * @return Entry count in @c [0, HIST_MAX].
 */
int sh_hist_count(void)
{
  return hist_count;
}

/**
 * @brief Look up history entry at @p idx (0 = oldest).
 *
 * @param idx  Zero-based index.
 * @return Pointer to internal storage (valid until next ::sh_hist_push), or
 *         @c NULL if @p idx is out of range.
 */
const char *sh_hist_at(int idx)
{
  if(idx < 0 || idx >= hist_count)
    return NULL;
  return history[idx];
}
