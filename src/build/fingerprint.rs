use anyhow::Result;

use crate::core::manifest::{self, Manifest};



pub fn create_fingerprint(manifest: &Manifest) -> Result<()> {



    Ok(())
}







// pub fn file_mtime_to fingerprint(mtime: std::time::SystemTime) -> u64 {
//     use std::time::{UNIX_EPOCH, Duration};

//     match mtime.duration_since(UNIX_EPOCH) {
//         Ok(duration) => duration.as_secs(),
//         Err(_) => 0,
//     }
// }