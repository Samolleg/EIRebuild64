# Selecting a build cache

`tools/build-task.ps1 -CacheRoot <path>` selects an isolated cache explicitly.
For callers that start this script in a child PowerShell process, including
`task accept`, the process environment variable `EIREBUILD64_BUILD_CACHE` supplies
the same parameter's default. An explicit `-CacheRoot` takes precedence; without
either value the standard `builds/rebuild64/cache` is used.

This changes only build storage. Existing path/reparse checks, exclusive locking,
cache hash validation and size limits remain in force. Do not reuse another
running build's mutable `work` directory. A temporary acceptance cache can live
under the parent task, outside its generated `accept-main-*/wt` build task; it is
removed by the normal task closeout. Set the variable only for that process.
