use std::{result::Result, path::{Path, PathBuf}, fs, process::Command};

use crate::core::manifest::load_manifest_from_cwd;

pub fn execute() {
    // manifest 로드
    let manifest = match load_manifest_from_cwd() {
        Ok(m) => m,
        Err(e) => {
            eprintln!("{}", e);
            return;
        }
    };
    let project_name = &manifest.package.name;
    let target_dir = Path::new("target").join("debug");
    fs::create_dir_all(&target_dir).expect("Failed to create target dir");
    let output_path = target_dir.join(project_name);

        // include / lib 경로와 링크 라이브러리 추출
        let mut include_paths: Vec<PathBuf> = Vec::new();
        let mut lib_paths: Vec<PathBuf> = Vec::new();
        let mut link_libs: Vec<String> = Vec::new();

        for (name, _ver) in &manifest.dependencies {
            let dep_root = dirs::home_dir().unwrap().join(".cppargo").join("packages").join(name);

            let include_dir = dep_root.join("include");
            if include_dir.exists() {
                include_paths.push(include_dir);
            }
            let lib_dir = dep_root.join("lib");
            if lib_dir.exists() {
                lib_paths.push(lib_dir.clone());
                if let Ok(entries) = fs::read_dir(&lib_dir) {
                    for entry in entries.flatten() {
                        if let Ok(fname) = entry.file_name().into_string() {
                            if fname.starts_with("lib") && fname.ends_with(".a") {
                                let libname = fname.trim_start_matches("lib").trim_end_matches(".a");
                                link_libs.push(format!("-l{}", libname));
                            } else if fname.starts_with("lib") && fname.ends_with(".so") {
                                let libname = fname.trim_start_matches("lib").trim_end_matches(".so");
                                link_libs.push(format!("-l{}", libname));
                            } else if fname.ends_with(".lib") {
                                let libname = fname.trim_end_matches(".lib");
                                link_libs.push(format!("-l{}", libname));
                            }
                        }
                    }
                }
            }
        }

        println!("Building `{}`...", project_name);

        let mut cmd = Command::new("g++");
        cmd.arg("-std=c++17");

        for cpp in glob::glob("src/*.cpp").unwrap().filter_map(Result::ok) {
            cmd.arg(cpp);
        }
        for inc in &include_paths {
            cmd.arg(format!("-I{}", inc.display()));
        }
        for libp in &lib_paths {
            cmd.arg(format!("-L{}", libp.display()));
        }
        for l in &link_libs {
            cmd.arg(l);
        }
        cmd.arg("-o").arg(&output_path);

        println!("> {:?}", cmd);

        let status = cmd.status().expect("Failed to run g++");
        if status.success() {
            println!("Build finished: {}", output_path.display());
        } else {
            eprintln!("Build failed");
        }
}
