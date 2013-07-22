use_relative_paths = True

vars = {
  # Override root_dir in your .gclient's custom_vars to specify a custom root
  # folder name.
  "root_dir": "trunk",

  # Use this llvm_url variable only if there is an internal mirror for it.
  # If you do not know, use the full path while defining your new deps entry.
  "llvm_url": "http://llvm.org/svn/llvm-project/llvm",
  "clang_url": "http://llvm.org/svn/llvm-project/cfe",
  "dragonegg_url": "http://llvm.org/svn/llvm-project/dragonegg",

  "llvm_revision": "178655",
  "clang_revision": "178511",
  "dragonegg_revision": "179508",
}

# NOTE: Prefer revision numbers to tags for svn deps. Use http rather than
# https; the latter can cause problems for users behind proxies.
deps = {
  "llvm":
    Var("llvm_url") + "/trunk@" + Var("llvm_revision"),
  "tools/clang":
    Var("clang_url") + "/trunk@" + Var("clang_revision"),
  "dragonegg":
    Var("dragonegg_url") + "/trunk@" + Var("dragonegg_revision"),
}

