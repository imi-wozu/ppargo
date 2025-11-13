use std::collections::HashMap;

use crate::core::manifest::{Manifest, Package, Toolchain};


pub fn crate_manifest(name: &str) -> Manifest {
    use crate::core::manifest::Features;
    
    Manifest {
        package: Package {
            name: name.to_string(),
            version: "0.1.0".to_string(),
            edition: "cpp17".to_string(),
        },
        dependencies: HashMap::new(),
        toolchain: Toolchain::default(),
        features: Features::default(),
    }
}

pub fn get_main_template() -> &'static str {
    r#"#include <iostream>

int main() {
    std::cout << "Hello, world!" << std::endl;
    return 0;
}"#
}

pub fn get_gitignore_template() -> &'static str {
    r#"/target"#
}