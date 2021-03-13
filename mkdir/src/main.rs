use std::fs;
use std::io;

fn main() {
    let mut dirname = String::new();

    io::stdin().read_line(&mut dirname)
        .expect("Failed to read line");

    do_mkdir(dirname);
}

fn do_mkdir(dirname: String){
    fs::create_dir(dirname);
}