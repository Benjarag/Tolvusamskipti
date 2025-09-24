# TSAM Assignment 3 - Port Forward, Starboard Back

## Group Members
- Benjamin Ragnarsson (benjaminr23)
- Sindri Björnsson (sindrib23) 
- Oliver (oliver23)

## Files Included
- `scanner.cpp` - UDP port scanner
- `puzzlesolver.cpp` - Main puzzle solver
- `Makefile` - Build system
- `master.sh` - Automation script
- `README.md` - This file

## Compilation
```bash
make
```

## Usage
To run everything:
```bash
chmod +x master.sh
sudo ./master.sh
```

To run the UDP port scanner:
```bash
./scanner 130.208.246.98 4000 4100
```

To execute the puzzle solver for example ports:
```bash
sudo ./puzzlesolver 130.208.246.98 4008 4022 4034 4080
```

## Notes
- Ensure all dependencies are installed before running the `make` command.
- The `master.sh` script automates the process of scanning and solving puzzles. Run it as follows:
```bash
chmod +x master.sh
sudo ./master.sh
```
## IMPORTANT: 
1. The script must be run with `sudo` to allow raw socket operations.
2. The program must run in the university network to access the evil port.
3. The program has multiple retry mechanisms to handle potential packet loss. If it fails, after several attempts and does not finish and successfully solve the puzzle, please try running it again.

## Contact
For any questions or issues, please contact the group members via their university emails.