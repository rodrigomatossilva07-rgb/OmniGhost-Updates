#![allow(dead_code)]
#![allow(unused_imports)]

use std::fs::File;
use std::path::PathBuf;
use std::process::ExitCode;
use std::str::FromStr;
use std::time::Instant;

use anyhow::Result;

use clap::{ArgAction, Parser};

use log::{LevelFilter, info};

use memflow::prelude::v1::*;

use simplelog::*;

use output::Output;

mod analysis;
mod memory;
mod output;
mod source2;

#[derive(Debug, Parser)]
#[command(author, version)]
struct Args {
    /// The name of the memflow connector to use.
    #[arg(short, long)]
    connector: Option<String>,

    /// Additional arguments to pass to the memflow connector.
    #[arg(short = 'a', long)]
    connector_args: Option<String>,

    /// The types of files to generate.
    #[arg(
        short,
        long,
        value_delimiter = ',',
        default_values = ["cs", "hpp", "json", "rs", "zig"]
    )]
    file_types: Vec<String>,

    /// The number of spaces to use per indentation level.
    #[arg(short, long, default_value_t = 4)]
    indent_size: usize,

    /// The output directory to write the generated files to.
    #[arg(short, long, default_value = "output")]
    output: PathBuf,

    /// The name of the game process.
    #[arg(short, long, default_value = "cs2.exe")]
    process_name: String,

    /// Increase logging verbosity. Can be specified multiple times.
    #[arg(short, long, action = ArgAction::Count)]
    verbose: u8,

    /// Prevent creation of the cs2-dumper.log file.
    #[arg(short, long)]
    no_log_file: bool,
}

fn main() -> ExitCode {
    match run(Args::parse()) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("Erro: {error:#}");
            pause_standalone_console();
            ExitCode::FAILURE
        }
    }
}

#[cfg(windows)]
fn pause_standalone_console() {
    use std::io::{self, IsTerminal};

    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn GetConsoleProcessList(processes: *mut u32, capacity: u32) -> u32;
    }

    if !io::stdin().is_terminal() {
        return;
    }

    let mut processes = [0; 2];
    // SAFETY: the writable array has space for both process IDs.
    if unsafe { GetConsoleProcessList(processes.as_mut_ptr(), 2) } == 1 {
        eprintln!("Prime Enter para fechar esta janela.");
        let _ = io::stdin().read_line(&mut String::new());
    }
}

#[cfg(not(windows))]
fn pause_standalone_console() {}

fn run(args: Args) -> Result<()> {
    let level_filter = match args.verbose {
        0 => LevelFilter::Error,
        1 => LevelFilter::Warn,
        2 => LevelFilter::Info,
        3 => LevelFilter::Debug,
        _ => LevelFilter::Trace,
    };

    let mut loggers: Vec<Box<dyn SharedLogger>> = vec![TermLogger::new(
        level_filter,
        Config::default(),
        TerminalMode::Mixed,
        ColorChoice::Auto,
    )];

    // Create the log file by default.
    if !args.no_log_file {
        loggers.push(WriteLogger::new(
            LevelFilter::Info,
            Config::default(),
            File::create("cs2-dumper.log")?,
        ));
    }

    CombinedLogger::init(loggers)?;

    let conn_args = args
        .connector_args
        .map(|s| ConnectorArgs::from_str(&s).expect("unable to parse connector arguments"))
        .unwrap_or_default();

    let mut os = match args.connector {
        Some(conn) => {
            let mut inventory = Inventory::scan();

            inventory
                .builder()
                .connector(&conn)
                .args(conn_args)
                .os("win32")
                .build()?
        }
        None => {
            #[cfg(windows)]
            {
                memflow_native::create_os(&OsArgs::default(), LibArc::default())?
            }
            #[cfg(not(windows))]
            {
                panic!("no connector specified")
            }
        }
    };

    let mut process = match os.process_by_name(&args.process_name) {
        Ok(process) => process,
        Err(memflow::error::Error(_, memflow::error::ErrorKind::ProcessNotFound)) => {
            anyhow::bail!(
                "Não encontrei o processo {}. Abre o jogo e tenta novamente.",
                args.process_name
            );
        }
        Err(error) => return Err(error.into()),
    };

    let now = Instant::now();

    let result = analysis::analyze_all(&mut process)?;
    let output = Output::new(&args.file_types, args.indent_size, &args.output, &result)?;

    output.dump_all(&mut process)?;

    info!("analysis completed in {:.2?}", now.elapsed());

    Ok(())
}
