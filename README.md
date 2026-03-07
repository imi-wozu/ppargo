## ppargo

Cargo-like C++ build system and package manager.

### Requirements

- `clang++`
- `vcpkg`

### Commands

```text
  add <name>                 Add dependencies to a ppargo.toml manifest file 
  build|b [-r]               Compile a local package and all of its dependencies 
  check|c                    Check a local package and all of its dependencies for errors
  new <name>                 Create a new ppargo package
  init                       Create a new ppargo package in an existing directory
  remove <dep>               Remove dependencies from a manifest file
  update [dep]               Update dependencies
  run|r [-r]                 Run a binary or example of the local package       
  version                    Print version
  help                       Print this help
```
### Manifest Contract

- `[package]`
- `[toolchain]`
- `[dependencies]`
- `[dev-dependencies]`
- `[build-dependencies]`
- `[registries]`
- `[features]`
- `[build]`

Backends:

- `package_manager = "vcpkg"`
- `package_manager = "ppargo"`

Notes:
- This project has only been tested in a Windows environment.
- Some packages have names that differ from the actual .lib files. When adding, specify the .lib file name directly instead of using the add command.
- `package_manager` implies package support. Omit `packages = true` when a manager is selected; set `packages = false` only to override and disable packages explicitly.

