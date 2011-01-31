redo-ifchange $1.cc
INCLUDE="-I/usr/local/include/node -I/opt/local/include -I../deps/mpool-2.1.0/"
g++ $INCLUDE -MD -MF $3.deps.tmp -c -o $3 $1.cc
DEPS=$(sed -e "s/^$3://" -e 's/\\//g' <$3.deps.tmp)
rm -f $3.deps.tmp
redo-ifchange $DEPS
