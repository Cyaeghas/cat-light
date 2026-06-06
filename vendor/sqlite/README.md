# SQLite Amalgamation

This directory vendors the SQLite 3.53.2 amalgamation:

- `sqlite3.c`
- `sqlite3.h`
- `sqlite3ext.h`

Source archive:

```text
https://www.sqlite.org/2026/sqlite-amalgamation-3530200.zip
```

SQLite is public domain. The bundled source is used when building with:

```text
CAT_LIGHT_ENABLE_SQLITE=ON
CAT_LIGHT_USE_BUNDLED_SQLITE=ON
```

Set `CAT_LIGHT_USE_BUNDLED_SQLITE=OFF` to use `find_package(SQLite3)` instead.
