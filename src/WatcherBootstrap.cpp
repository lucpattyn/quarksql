#include "RequirementsWatcher.h"

// Start the requirements watcher during static initialization.
// It runs in a detached thread and will continue for the process lifetime.
// If V8 is not ready yet, hot-loading will be skipped; subsequent changes
// will be hot-loaded once V8 is initialized in main.cpp.
struct __QuarksqlWatcherBootstrap {
    __QuarksqlWatcherBootstrap() {
        StartRequirementsWatcher("requirements");
    }
};

static __QuarksqlWatcherBootstrap __quarksql_watcher_bootstrap_instance;

