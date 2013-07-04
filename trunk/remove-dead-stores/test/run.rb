filename = ARGV.first

%x[clang -emit-llvm -O0 -c -S #{filename} -o teste.ll]
# %x[clang -emit-llvm -O0 -c -S #{filename} -o stores.ll]
%x[opt -mem2reg teste.ll -S > stores.ll]
puts %x[opt -load /llvm/Debug+Asserts/lib/DeadStoreElimination.dylib -dead-store-elimination -S -debug-only=dse stores.ll]
