#!/usr/bin/env python3

import ast
import os
import subprocess
import sys

def run_test(test_name):
    print('Running test ' + test_name)
    test_path = os.path.join(tests_dir, test_name + '.hgn')
    lines = list(open(test_path))
    test_command_start = 0
    if len(lines) <= 0 or ((len(lines[0]) == 0 or lines[0][0] != '※') and (len(lines[-1]) == 0 or lines[-1][0] != '※')):
        raise RuntimeError('No test commands given')
    if len(lines[0]) == 0 or lines[0][0] != '※':
        for i in reversed(range(len(lines))):
            if len(lines[i]) > 0 and lines[i][0] == '※':
                test_command_start = i
            else:
                break
    expected_result = 'success'
    expected_output = ''
    for i in range(test_command_start, len(lines)):
        if len(lines[i]) > 0 and lines[i][0] == '※':
            if lines[i][1] == ' ':
                expected_output += lines[i][2:]
            elif lines[i][1] == '-':
                if ':' in lines[i]:
                    command, arg = lines[i][2:-1].split(':', maxsplit=1)
                else:
                    command, arg = lines[i][2:-1], None
                if command == 'fail':
                    expected_result = 'fail'
                elif command == 'cfail':
                    expected_result = 'cfail'
                elif command == 'esc':
                    expected_output += arg.replace('\\0', '\0')
                else:
                    raise RuntimeError('Invalid test command')
            else:
                raise RuntimeError('Invalid test command')
        else:
            break
    output_path = test_path + '.out'
    compile_stderr_arg = subprocess.DEVNULL if expected_result == 'cfail' else None
    compile_result = subprocess.run([compiler, test_path, '-o' + output_path], stderr=compile_stderr_arg)
    if expected_result == 'cfail':
        success = compile_result.returncode != 0
    else:
        if compile_result.returncode == 0:
            program_stderr_arg = subprocess.DEVNULL if expected_result == 'fail' else None
            program_result = subprocess.run([output_path], stdout=subprocess.PIPE, stderr=program_stderr_arg)
            if expected_result == 'fail':
                success = program_result.returncode != 0
            else:
                success = program_result.returncode == 0
            try:
                program_stdout = program_result.stdout.decode('utf-8')
                if program_stdout != expected_output:
                    print('Got output:')
                    print(program_stdout)
                    success = False
            except UnicodeDecodeError:
                print('Got output:')
                print(program_result.stdout)
                success = False
        else:
            success = False
    try:
        os.remove(output_path)
    except FileNotFoundError:
        pass
    if not success:
        print('\033[31mTest failed\033[39m')

args = sys.argv[1:]
if len(args) == 2:
    compiler, tests_dir = args
    test_name = None
elif len(args) == 3:
    compiler, tests_dir, test_name = args

if test_name is None:
    for test in os.listdir(tests_dir):
        if test.endswith('.hgn'):
            test_name = test[:-4]
            run_test(test_name)
else:
    run_test(test_name)
