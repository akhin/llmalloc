#!/usr/bin/python
import os
import time
import shutil
import glob
from pathlib import Path
from sys import platform as _platform

class Utility:
    CONSOLE_RED = '\033[91m'
    CONSOLE_BLUE = '\033[94m'
    CONSOLE_YELLOW = '\033[93m'
    CONSOLE_END = '\033[0m'
    CONSOLE_GREEN = '\033[92m'

    @staticmethod
    def get_file_content(file_name):
        ret = ""
        if Utility.is_valid_file(file_name):
            ret = open(file_name, 'r').read()
        return ret
        
    @staticmethod
    def is_valid_file(file_path):
        return os.path.isfile(file_path)

    @staticmethod
    def write_colour_message(message, colour_code):
        print(colour_code + message + Utility.CONSOLE_END)

    @staticmethod
    def execute_shell_command(command):
        os.system(command)

def delete_file_or_folder_if_exists(path):
    try:
        if os.path.isfile(path):
            os.remove(path)
        elif os.path.isdir(path):
            shutil.rmtree(path)
    except OSError as e:
        print(f"Error: {e}")

def delete_files_with_pattern(pattern):
    files = glob.glob(pattern)
    for file_path in files:
        try:
            if os.path.isfile(file_path):
                os.remove(file_path)
        except OSError as e:
            print(f"Error deleting {file_path}: {e}")

def perform_single_stress_test(mode):
    print("")
    Utility.write_colour_message("Performing stress test mode " + str(mode), Utility.CONSOLE_BLUE)
    print("")

    executable_command = ""
    if _platform == "win32":
        executable_command = "stress_tests.exe " + str(mode) + " >> results.txt"
    elif _platform == "linux" or _platform == "linux2":
        executable_command = "./stress_tests " + str(mode) + " >> results.txt"

    Utility.execute_shell_command(executable_command)

def check_results():
    if Utility.is_valid_file("./results.txt") is False:
        print("")
        Utility.write_colour_message("Stress tests failed !!!", Utility.CONSOLE_RED)
        print("")
        return
    if os.stat("./results.txt").st_size == 0:
        print("")
        Utility.write_colour_message("Stress tests failed !!!", Utility.CONSOLE_RED)
        print("")
        return

    content = Utility.get_file_content("./results.txt")    

    if content.count("All good") != 4:
        print("")
        Utility.write_colour_message("Stress tests failed !!!", Utility.CONSOLE_RED)
        print("")
        return

    Utility.write_colour_message("All stress tests passed", Utility.CONSOLE_GREEN)

def main():
    try:
        start_time = time.time()
        
        os.chdir("./stress_tests")
    
        delete_files_with_pattern("*.exe")
        delete_file_or_folder_if_exists("results.txt")
        delete_file_or_folder_if_exists(".vs")
        delete_file_or_folder_if_exists("x64")
        
        if _platform == "linux" or _platform == "linux2":
            Utility.execute_shell_command("make clean")
            Utility.execute_shell_command("make debug")
        elif _platform == "win32":
            Utility.execute_shell_command("build_msvc.bat no_pause")

        for i in range(4):
            perform_single_stress_test(i)
            
        check_results()

        # DISPLAY ELAPSED TIME
        end_time = time.time()
        elapsed_time = end_time - start_time
        minutes = int(elapsed_time // 60)
        seconds = int(elapsed_time % 60)
        print("")
        print(f"Elapsed time: {minutes} minutes and {seconds} seconds")
        print("")

    except ValueError as err:
        print(err.args)

#Entry point
if __name__ == "__main__":
    main()