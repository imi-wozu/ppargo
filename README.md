## ppargo
 
Rust cargo-like C++ build & package manager 

### Requirements 
clang 
https://github.com/llvm/llvm-project/releases

vcpkg

### Instalation


### Usage 
argo + command

#### command list 
 ```
  new    Create a new ppargo package
  init   Create a new ppargo package in an existing directory
  add    Add dependencies to a manifest file
  build  Complie the current package
  run    Run the package
  help   Print this message or the help of the given subcommand(s)
 ```


### Notice 
This project has only been tested in a Windows environment.
Some packages have names that differ from the actual .lib files. When adding, specify the .lib file name directly instead of using the add command.