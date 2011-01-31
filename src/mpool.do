redo-ifchange `cat ../deps/mpool-2.1.0/*.c`
cd ../deps/mpool-2.1.0
make
mv libmpool.a $3
