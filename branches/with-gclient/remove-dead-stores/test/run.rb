filename = ARGV.first

%x[clang -emit-llvm -O0 -c -S #{filename} -o teste.ll]
# %x[opt -mem2reg teste.ll -S > stores.ll]
%x[opt teste.ll -S > stores.ll]
puts %x[opt -dead-store-elimination -S -debug-only=dead-store-elimination -stats stores.ll > teste.ll ]
