redo-ifchange $(find ../deps/mpool-2.1.0/ -name '*.c')
cd ../deps/mpool-2.1.0
make
cat libmpool.a
