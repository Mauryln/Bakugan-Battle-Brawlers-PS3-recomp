#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Called from the frame clock. Handles the optional F5/F9 scene keys when
 * PS3_SCENE_DIR is set. No-op otherwise. */
void scene_state_tick(void);

#ifdef __cplusplus
}
#endif
