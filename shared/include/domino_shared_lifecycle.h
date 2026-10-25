#ifndef DOMINO_SHARED_LIFECYCLE_H
#define DOMINO_SHARED_LIFECYCLE_H

/** @brief 线程模块的串行生命周期状态；Stop 后只能 Destroy，再次使用须重新 Init。 */
typedef enum DominoModuleLifecycleState {
    DOMINO_MODULE_UNINITIALIZED = 0,
    DOMINO_MODULE_INITIALIZED,
    DOMINO_MODULE_STARTED,
    DOMINO_MODULE_STOPPED,
} DominoModuleLifecycleState;

#endif
