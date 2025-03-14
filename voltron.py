#!/usr/bin/python
#
#   VOLTRON / ボルトロン
#
#   Bakes sources specified in voltron.txt into a single-header file
#   for easy integration and also to benefit from compiler optimisations as much as possible
#
import os
import os.path
import sys
import re
from sys import platform as _platform
import operator

class Utility:
    CONSOLE_RED = '\033[91m'
    CONSOLE_BLUE = '\033[94m'
    CONSOLE_YELLOW = '\033[93m'
    CONSOLE_END = '\033[0m'

    @staticmethod
    def save_to_file(file_name, text):
        Utility.delete_file_if_exists(file_name)
        
        with open(file_name, "w", newline='') as text_file:
            text_file.write(text)

    @staticmethod
    def change_working_directory_to_script_location():
        absolutePath = os.path.abspath(__file__)
        dirName = os.path.dirname(absolutePath)
        os.chdir(dirName)
        
    @staticmethod
    def is_valid_file(file_path):
        return os.path.isfile(file_path)

    @staticmethod
    def delete_file_if_exists(file_path):
        try:
            os.remove(file_path)
        except OSError:
            pass

    @staticmethod
    def write_colour_message(message, colour_code):
        print(colour_code + message + Utility.CONSOLE_END)

    @staticmethod
    def write_trace_message(message):
        Utility.write_colour_message(message, Utility.CONSOLE_BLUE)

    @staticmethod
    def write_error_message(message):
        Utility.write_colour_message(message, Utility.CONSOLE_RED)

    @staticmethod
    def write_info_message(message):
        Utility.write_colour_message(message, Utility.CONSOLE_YELLOW)

class Converter:
    def __init__(self):
        self.includes = []
        self.header_lines = []
        self.system_libraries_lines = []
        self.source_root_path = ""
        self.output_file_name=""
        self.namespace=""

    def process_input_file(self, input_file_name):
        if Utility.is_valid_file(input_file_name) is False:
            Utility.write_error_message("Invalid input file :" + input_file_name )
        with open(input_file_name) as fp:
            line_number=0
            processing_header = False
            processing_system_libraries = False
            processing_inclusions = False
            for line in fp:
                
                if line[0] == '#' and (line.startswith('#include') == False) and (line.startswith('#ifdef') == False) and (line.startswith('#endif')== False) and (line.startswith('#if')== False) and (line.startswith('#elif')== False):
                    continue
                line_number= line_number + 1
                line = line.strip()
                
                # SOURCE PATH
                if line.startswith("source_path="):
                    tokens = line.split('=')
                    self.source_root_path = tokens[1]
                # OUTPUT FILE NAME
                elif line.startswith("output_header="):
                    tokens = line.split('=')
                    self.output_file_name = tokens[1]
                # NAMESPACE
                elif line.startswith("namespace="):
                    tokens = line.split('=')
                    self.namespace = tokens[1]
                elif line == '[HEADER]':
                    processing_header = True
                elif line == '[SYSTEM_LIBRARIES]':
                    processing_system_libraries = True
                    processing_header = False
                elif line == '[INCLUSIONS]':
                    processing_inclusions = True
                    processing_system_libraries = False
                else:
                    if processing_inclusions is True:
                        self.includes.append(line)
                    if processing_header is True:
                        self.header_lines.append(line)
                    if processing_system_libraries is True:
                        self.system_libraries_lines.append(line)

    @staticmethod
    def clean_unnecessary_newlines_and_tabs(source, tab_size=4):
        # Clean unnecessary newlines (more than 2 consecutive newlines)
        clean_source = re.sub(r'\n{3,}', '\n\n', source)
        # Replace tabs with spaces (default 4 spaces per tab)
        spaces = ' ' * tab_size
        clean_source = clean_source.replace('\t', spaces)
        return clean_source

    def save_to(self):
        content= ""
        errors = ""

        # WRITING HEADER
        for header_line in self.header_lines:
            content += header_line
            content += "\n"

        # WRITING INCLUDE PROTECTION
        output_file_name_without_extension = os.path.splitext(os.path.basename(self.output_file_name))[0]
        include_protection_define = "_" + output_file_name_without_extension + "_H_"
        include_protection_define = include_protection_define.upper()

        content += "#ifndef " + include_protection_define
        content += "\n"
        content += "#define " + include_protection_define
        content += "\n\n"

        # WRITING SYSTEM LIBRARIES
        for system_library_line in self.system_libraries_lines:
            content += system_library_line
            content += "\n"

        content += "\n"

        # WRITING NAMESPACE
        if len(self.namespace)>0:
            content += "namespace " + self.namespace
            content += "\n"
            content += "{"
            content += "\n"

        for include in self.includes:
            actual_include = include
            include_dependency = ""
            if "," in include:
                parts = include.split(",")
                actual_include = parts[0]
                include_dependency = parts[1]

            target_file = self.source_root_path + actual_include

            if Utility.is_valid_file(target_file) is True:

                if len(include_dependency)>0:
                    content += "#ifdef " + include_dependency + "\n"

                with open(target_file) as fp:
                    for line in fp:

                        if "// VOLTRON_EXCLUDE" in line:
                            continue

                        has_voltron_include= False

                        if "// VOLTRON_INCLUDE" in line:
                            has_voltron_include = True

                        if "#include" in line and has_voltron_include == False:
                            continue
                            
                        if "using namespace" in line:
                            continue

                        if "_H_" in line and "#ifndef" in line:
                            continue

                        if "_H_" in line and "#define" in line:
                            continue

                        if "// VOLTRON_NAMESPACE_EXCLUSION_START" in line and len(self.namespace)>0:
                            content += "\n} // NAMESPACE END \n"
                            continue

                        if "// VOLTRON_NAMESPACE_EXCLUSION_END" in line and len(self.namespace)>0:
                            content += "namespace " + self.namespace
                            content += "\n"
                            content += "{"
                            content += "\n"
                            continue

                        content += line

                # Remove the last line #endif from the file
                lines = content.splitlines()
                if len(lines) >= 1:
                    lines = lines[:-1]
                    content = "\n".join(lines)
                content += "\n"
            else:
                errors += "Could not write " + target_file + "\n"

                if len(include_dependency)>0:
                    content += "#endif \n"

        content += "\n"

        if len(self.namespace)>0:
            content += "}"
            content += "\n"

        content += "#endif"

        content = Converter.clean_unnecessary_newlines_and_tabs(content)
        Utility.save_to_file(self.output_file_name, content)
        if len(errors) > 0:
            Utility.save_to_file("errors.txt", errors)

def main():
    ret_val = 0
    try:
        input_file = sys.argv[1] if len(sys.argv) > 1 else "voltron.txt"

        Utility.change_working_directory_to_script_location()

        converter = Converter()
        converter.process_input_file(input_file)
        converter.save_to()
    except ValueError as err:
        Utility.write_error_message( str(err))
        ret_val = -1
    except Exception as ex:
        Utility.write_error_message( str(ex))
        ret_val = -1    
    finally:
        return ret_val


# Entry point
if __name__ == "__main__":
    main()