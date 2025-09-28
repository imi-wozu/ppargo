// use std::{collections::HashMap, fs, path::Path};

// use crate::core::manifest::{Manifest, Package};

// pub fn execute(name: &str) {
//     let dir = Path::new(name);
//     fs::create_dir_all(dir.join("src")).expect("Failed to create project directory");

//     // create Cppargo.toml
//     let manifest = Manifest {
//         package: Package {
//             name: name.to_string(),
//             version: "0.1.0".to_string(),
//         },
//         dependencies: HashMap::new(),
//     };

//     let toml_str = toml::to_string(&manifest).expect("Failed to serialize manifest");
//     fs::write(dir.join("Cppargo.toml"), toml_str).expect("Failed to write Cppargo.toml");

//     // basic main.cpp
//     let main_cpp = r#"#include <iostream>

// int main() {
//     std::cout << "Hello, world!" << std::endl;
//     return 0;
// }"#;

//     fs::write(dir.join("src").join("main.cpp"), main_cpp).expect("Failed to write main.cpp");

//     println!("Initialized C++ project `{}`", name);
// }
