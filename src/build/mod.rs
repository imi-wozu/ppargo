use anyhow::{Context, Result, bail};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use walkdir::WalkDir;

use crate::core::get_manifest;

pub mod fingerprint;

fn get_compiler_path() -> Result<PathBuf> {
    which::which(&get_manifest().toolchain.compiler).context(format!("Compiler '{}' not found in PATH", get_manifest().toolchain.compiler))
}

pub fn build(release: bool) -> Result<()> {
    if (get_manifest().features.packages) {
       // crate::package::install_dependencies(&get_manifest())?;
    }

    let profile = if release { "release" } else { "debug" };
    let build_dir = crate::core::get_build_dir(profile);

    // Create build directory
    fs::create_dir_all(&build_dir).context("Failed to create build directory")?;

    // Collect source files
    let source_files = collect_source_files()?;

    if source_files.is_empty() {
        bail!("No source files found in src/");
    }

    // Compile sources
    let object_files = compile_sources(&source_files, &build_dir, release)?;

    // Link
    link_executable(&object_files, &build_dir, release)?;

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

fn compile_sources(
    sources: &[PathBuf],
    build_dir: &Path,
    release: bool,
) -> Result<Vec<PathBuf>> {
    let obj_dir = build_dir.join("obj");
    fs::create_dir_all(&obj_dir).context("Failed to create object files directory")?;

    // Create obj directory
    let mut object_files = Vec::new();

    for source in sources {
        let obj_file = get_object_file_path(source, &obj_dir)?;

        if needs_rebuild(source, &obj_file)? {
            compile_single_file(source, &obj_file, release)?;
        }

        object_files.push(obj_file);
    }

    Ok(object_files)
}

fn get_object_file_path( source: &Path, obj_dir: &Path) -> Result<PathBuf> {
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

fn needs_rebuild(source: &Path, object: &Path) -> Result<bool> {
    if !object.exists() {
        return Ok(true);
    }

    let source_modified = fs::metadata(source)?.modified()?;
    let object_modified = fs::metadata(object)?.modified()?;

    Ok(source_modified > object_modified)
}

fn compile_single_file(source: &Path, output: &Path, release: bool) -> Result<()> {
    let mut cmd = Command::new(get_compiler_path()?);

    // Basic flags
    cmd.arg("-c").arg(source).arg("-o").arg(output);

    // C++ standard
    let std_flag = match crate::core::get_manifest().package.edition.as_str() {
        "cpp20" => "-std=c++20",
        "cpp23" => "-std=c++23",
        "cpp26" => "-std=c++26",
        _ => "-std=c++17",
    };
    cmd.arg(std_flag);

    // Optimization
    if release {
        cmd.arg("-03");
    } else {
        cmd.arg("-O0").arg("-g");
    }

    // Warnings
    cmd.arg("-Wall").arg("-Wextra");

    // Include paths
    let include_dir = crate::core::get_root()
        .join("packages")
        .join(crate::util::detect_triplet())
        .join("include");
    if include_dir.exists() {
        cmd.arg(format!("-I{}", include_dir.display()));
    }

    // Package include paths (if enabled)
    // if let Some(pm) = &self.package_manager {
    //     // for path in pm.get_include_path() {
    //     //     cmd.arg(format!("-I{}", path.display()));
    //     // }
    // }

    let output_result = cmd.output().context("Failed to execute compiler")?;

    if !output_result.status.success() {
        eprintln!("{}", String::from_utf8_lossy(&output_result.stderr));
        bail!("Compilation failed for {:?}", source);
    }

    Ok(())
}

fn link_executable( objects: &[PathBuf], build_dir: &Path, release: bool) -> Result<()> {
    let binary_name = crate::core::get_binary_name();
    let output = build_dir.join(&binary_name);

    let mut cmd = Command::new(get_compiler_path()?);

    // Object files
    for obj in objects {
        cmd.arg(obj);
    }

    // Output
    cmd.arg("-o").arg(&output);

    // Optimization
    if release {
        cmd.arg("-O3");
        cmd.arg("-s"); // Strip symbols
    }

    // Library paths (if enabled)
    // if let Some(pm) = &self.package_manager {
    //     // for path in pm.get_lib_paths() {
    //     //     cmd.arg(format!("-L{}", path.display()));
    //     // }
    // }

    // Execute linking
    let output_result = cmd.output().context("Failed to execute linker")?;

    if !output_result.status.success() {
        eprintln!("{}", String::from_utf8_lossy(&output_result.stderr));
        bail!("Linking failed");
    }

    Ok(())
}

fn generate_compile_commands(
    sources: &[PathBuf],
    build_dir: &Path,
    release: bool,
) -> Result<()> {
    let mut commands = Vec::new();
    let profile = if release { "release" } else { "debug" };

    for source in sources {
        let mut cmd = Vec::new();
        cmd.push(get_compiler_path()?.to_string_lossy().to_string());
        cmd.push("-c".to_string());
        cmd.push(source.to_string_lossy().to_string());

        // C++ standard
        let std_flag = match crate::core::get_manifest().package.edition.as_str() {
            "cpp20" => "-std=c++20",
            "cpp23" => "-std=c++23",
            _ => "-std=c++17",
        };
        cmd.push(std_flag.to_string());

        // Optimization
        if release {
            cmd.push("-O3".to_string());
        } else {
            cmd.push("-O0".to_string());
            cmd.push("-g".to_string());
        }

        // Warnings
        cmd.push("-Wall".to_string());
        cmd.push("-Wextra".to_string());

        // Include paths
        let include_dir = crate::core::get_root().join("include");
        if include_dir.exists() {
            cmd.push(format!("-I{}", include_dir.display()));
        }

        commands.push(serde_json::json!({
            "directory": build_dir.to_string_lossy(),
            "command": cmd.join(" "),
            "file": source.to_string_lossy(),
        }));
    }

    let compile_commands_path = crate::core::get_root().join("compile_commands.json");
    let file = fs::File::create(&compile_commands_path)
        .context("Failed to create compile_commands.json")?;
    serde_json::to_writer_pretty(file, &commands)
        .context("Failed to write compile_commands.json")?;

    Ok(())
}
