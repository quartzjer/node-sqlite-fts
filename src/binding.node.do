DEPS="statement.o
      database.o
      sqlite3_bindings.o"
LDFLAGS="-L/usr/local/lib -L./ -lsqlite3"
redo-ifchange $DEPS
g++ -o $3 $DEPS $LDFLAGS -undefined dynamic_lookup 
