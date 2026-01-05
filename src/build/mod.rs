use anyhow::{Context, Result, bail};
use rayon::prelude::*;
use std::fmt::format;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Output};
use walkdir::WalkDir;

use crate::core::manifest::{Dependency, Manifest};
use crate::core::{get_build_dir, get_manifest, get_root, manifest};

pub mod fingerprint;

struct BuildContext {
    compiler: PathBuf,
    flags: Vec<String>,
    include_paths: Vec<PathBuf>,
    lib_paths: Vec<PathBuf>,
    release: bool,
}

pub fn build(release: bool) -> Result<()> {
    // init
    let manifest = get_manifest();
    let build_dir = get_build_dir(release);
    fs::create_dir_all(&build_dir).context("Failed to create build directory")?;

    //init vcpkg roots
    let vcpkg_installed = get_root()
        .join("packages")
        .join(crate::util::detect_triplet());

    let mut base_flags: Vec<String> = Vec::new();

    // C++ standard
    base_flags.push(
        match manifest.package.edition.as_str() {
            "cpp20" => "-std=c++20",
            "cpp23" => "-std=c++23",
            "cpp26" => "-std=c++26",
            _ => "-std=c++17",
        }
        .to_string(),
    );

    // Optimization
    if release {
        base_flags.push("-O3".to_string());
    } else {
        base_flags.push("-O0".to_string());
        base_flags.push("-g".to_string());
    }

    // Warnings
    base_flags.push("-Wall".to_string());
    base_flags.push("-Wextra".to_string());

    // collect sources
    let source_files = collect_source_files()?;
    if source_files.is_empty() {
        bail!("No source files found in src/");
    }

    // if (get_manifest().features.packages) {
    //     // crate::package::install_dependencies(&get_manifest())?;
    // }

    // make build config
    let ctx = BuildContext {
        compiler: which::which(&manifest.toolchain.compiler).context(format!(
            "Compiler '{}' not found in PATH",
            get_manifest().toolchain.compiler
        ))?,
        flags: base_flags,
        include_paths: vec![
            //crate::core::get_root().join("include"), // project include
            vcpkg_installed.join("include"), // vcpkg include
        ],
        lib_paths: vec![vcpkg_installed.join("lib")], // vcpkg lib
        release,
    };

    // Compile sources
    let object_files = compile_sources_parallel(&ctx, &source_files, &build_dir)?;

    // Link
    link_executable(&ctx, &object_files, &build_dir)?;

    copy_dlls(&manifest, &build_dir)?;

    Ok(())
}

fn collect_source_files() -> Result<Vec<PathBuf>> {
    let mut sources: Vec<PathBuf> = Vec::new();
    let src_dir = crate::core::get_root().join("src");

    if !src_dir.exists() {
        bail!("Source directory 'src/' does not exist");
    }

    for entry in WalkDir::new(&src_dir) {
        let entry = entry?;
        let path = entry.path();

        if path.is_file() {
            if let Some(ext) = path.extension() {
                if matches!(
                    ext.to_str(),
                    Some("cpp") | Some("cxx") | Some("cc") | Some("c")
                ) {
                    sources.push(path.to_path_buf());
                }
            }
        }
    }

    Ok(sources)
}

fn compile_sources_parallel(
    ctx: &BuildContext,
    sources: &[PathBuf],
    build_dir: &Path,
) -> Result<Vec<PathBuf>> {
    let mut obj_dir = build_dir.join("obj");
    fs::create_dir_all(&obj_dir).context("Failed to create object files directory")?;

    let object_files: Result<Vec<PathBuf>> = sources
        .par_iter()
        .map(|source| {
            let obj_file = get_object_file_path(source, &obj_dir)?;
            let dep_file = obj_file.with_extension("d");

            if needs_rebuild(source, &obj_file, &dep_file)? {
                compile_single_file(ctx, source, &obj_file, &dep_file)?;
            }
            Ok(obj_file)
        })
        .collect();

    object_files
}

// fn compile_sources(sources: &[PathBuf], build_dir: &Path, release: bool) -> Result<Vec<PathBuf>> {
//     let obj_dir = build_dir.join("obj");
//     fs::create_dir_all(&obj_dir).context("Failed to create object files directory")?;

//     // Create obj directory
//     let mut object_files = Vec::new();

//     for source in sources {
//         let obj_file = get_object_file_path(source, &obj_dir)?;

//         if needs_rebuild(source, &obj_file)? {
//             compile_single_file(source, &obj_file, release)?;
//         }

//         object_files.push(obj_file);
//     }

//     Ok(object_files)
// }

fn get_object_file_path(source: &Path, obj_dir: &Path) -> Result<PathBuf> {
    let relatives = source
        .strip_prefix(&crate::core::get_root())
        .unwrap_or(source);
    let mut obj_path = obj_dir.join(relatives);
    obj_path.set_extension("o");

    if let Some(parent) = obj_path.parent() {
        fs::create_dir_all(parent)?;
    }

    Ok(obj_path)
}

fn needs_rebuild(source: &Path, obj_file: &Path, dep_file: &Path) -> Result<bool> {
    if !obj_file.exists() || !dep_file.exists() {
        return Ok(true);
    }

    let obj_time = fs::metadata(obj_file)?.modified()?;
    let src_time = fs::metadata(source)?.modified()?;

    if src_time > obj_time {
        return Ok(true);
    }

    if let Ok(content) = fs::read_to_string(dep_file) {
        let parts: Vec<&str> = content
            .split_whitespace()
            .filter(|s| *s != "\\" && !s.ends_with(':'))
            .collect();

        for part in parts {
            let dep_path = Path::new(part);
            if dep_path.exists() {
                if let Ok(metadata) = fs::metadata(dep_path) {
                    if let Ok(dep_time) = metadata.modified() {
                        if dep_time > obj_time {
                            return Ok(true);
                        }
                    }
                }
            }
        }
    }

    Ok(false)
}

fn compile_single_file(
    ctx: &BuildContext,
    source: &Path,
    output: &Path,
    dep_file: &Path,
) -> Result<()> {
    let mut cmd = Command::new(&ctx.compiler);

    cmd.args(&ctx.flags)
        .arg("-c")
        .arg(source)
        .arg("-o")
        .arg(output);

    // Include paths
    for path in &ctx.include_paths {
        cmd.arg(format!("-I{}", path.display()));
    }

    cmd.arg("-MMD").arg("-MF").arg(dep_file);

    let output_result = cmd.output().context("Failed to execute compiler")?;

    if !output_result.status.success() {
        eprintln!(
            "Error compiling {:?}:\n{}",
            source,
            String::from_utf8_lossy(&output_result.stderr)
        );
        bail!("Compilation failed");
    }

    Ok(())
}

fn link_executable(ctx: &BuildContext, objects: &[PathBuf], build_dir: &Path) -> Result<()> {
    let binary_name = crate::core::get_binary_name();
    let output = build_dir.join(&binary_name);

    let mut cmd = Command::new(&ctx.compiler);

    cmd.arg("-fuse-ld=lld");

    // Object files
    for obj in objects {
        cmd.arg(obj);
    }

    // Output
    cmd.arg("-o").arg(&output);

    // Libraries paths
    for path in &ctx.lib_paths {
        cmd.arg(format!("-L{}", path.display()));
    }

    let mut libs_to_link = Vec::new();

    for (pkg_name, dep) in &get_manifest().dependencies {
        match dep {
            Dependency::Simple(_) => {
                libs_to_link.push(pkg_name.clone());
            }
            Dependency::Detailed(detail) => {
                if detail.libs.is_empty() {
                    libs_to_link.push(pkg_name.clone());
                } else {
                    libs_to_link.extend(detail.libs.clone());
                }
            }
        }
    }

    // Link libraries
    for lib in &libs_to_link {
        cmd.arg(format!("-l{}", lib));
    }

    // Optimization
    if ctx.release && !cfg!(windows) {
        cmd.arg("-s");
    }

    // Execute linking
    let output_result = cmd.output().context("Failed to execute linker")?;

    if !output_result.status.success() {
        eprintln!(
            "Linker Error:\n{}",
            String::from_utf8_lossy(&output_result.stderr)
        );
        bail!("Linking failed");
    }

    Ok(())
}

fn copy_dlls(manifest: &Manifest, build_dir: &Path) -> Result<()> {
    if !cfg!(windows) {
        return Ok(());
    }

    let vcpkg_bin = get_root()
        .join("packages")
        .join(crate::util::detect_triplet())
        .join("bin");

    if !vcpkg_bin.exists() {
        return Ok(());
    }

    for entry in fs::read_dir(&vcpkg_bin)? {
        let entry = entry?;
        let path = entry.path();

        if path.is_file() && path.extension().and_then(|s| s.to_str()) == Some("dll") {
            let dest = build_dir.join(path.file_name().unwrap());

            if should_copy(&path, &dest)? {
                fs::copy(&path, &dest).context(format!("Failed to copy {:?}", path.file_name()))?;
            }
        }
    }

    Ok(())
}

fn should_copy(src: &Path, dest: &Path) -> Result<bool> {
    if !dest.exists() {
        return Ok(true);
    }

    let src_time = fs::metadata(src)?.modified()?;
    let dest_time = fs::metadata(dest)?.modified()?;

    Ok(src_time > dest_time)
}
