{
  'targets': [
    {
      'target_name': 'sqlite3_bindings',
      'sources': [
        'src/sqlite3_bindings.cc',
        'src/database.cc',
        'src/statement.cc',
        'src/events.cc',
        'src/mpool.c',
        'deps/sqlite-amalgamation-3070500/sqlite3.c',
      ],
      'conditions': [
        ['OS=="win"', {
          'sources': [
            'deps/win/sys/mman.c',
          ],
          'include_dirs': [
            'deps/win',
          ],
        }],
      ],
      'defines': [
        'SQLITE_ENABLE_FTS3',
        'SQLITE_ENABLE_FTS3_PARENTHESIS',
      ],
      'include_dirs': [
        'src',
        'deps/sqlite-amalgamation-3070500',
      ],
      'cflags': [ '-O3' ],
    },
  ],
}
