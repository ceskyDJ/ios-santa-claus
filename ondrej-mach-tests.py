#!/usr/bin/python3

# Tests for project 2 from IOS course
# Author: Ondrej Mach
# Date: 17.04.2021

import subprocess
import sys
import random
import time
from enum import Enum
from time import perf_counter
from typing import Union
import argparse
import os
import shutil
import multiprocessing
import threading
import signal

printLock = multiprocessing.Lock()

class Arguments:
    def __init__(self, NE=5, NR=5, TE=100, TR=100):
        self.NE = NE
        self.NR = NR
        self.TE = TE
        self.TR = TR


baseArguments = [
    Arguments(20, 5, 0, 50),
    Arguments(3, 5, 0, 0),
    Arguments(10, 10, 10, 10),
    Arguments(5, 4, 100, 100),
    Arguments(8, 2, 50, 0),
    Arguments(2, 1, 0, 10)
]

extendedArguments = [
    Arguments(999, 19, 0, 0),
    Arguments(100, 10, 10, 200),
    Arguments(80, 15, 100, 0),
    Arguments(999, 1, 0, 10),
    Arguments(2, 19, 0, 100),
    Arguments(999, 19, 10, 100),
]

def printWithLock(message: str):
    printLock.acquire()
    print(message)
    printLock.release()


class Santa:
    class State(Enum):
        NOT_STARTED = 0
        SLEEPING = 1
        HELPING_ELVES = 2
        HITCHING_RDS = 3
        GONE = 4

    def __init__(self):
        self.state = self.State.NOT_STARTED


class Elf:
    class State(Enum):
        NOT_STARTED = 0
        WORKING_ALONE = 1
        AWAITING_HELP = 2
        ON_VACATION = 3

    def __init__(self, ID=0):
        self.state = self.State.NOT_STARTED
        self.ID = ID


class Reindeer:
    class State(Enum):
        NOT_STARTED = 0
        ON_VACATION = 1
        BACK_HOME = 2
        HITCHED = 3

    def __init__(self, ID=0):
        self.state = self.State.NOT_STARTED
        self.ID = ID


class LineCounter:
    def __init__(self):
        self.expectedNumber = 1
        pass

    def read(self, num):
        if int(num) != self.expectedNumber:
            printWithLock(f'Expected line number {self.expectedNumber}')
            raise

        self.expectedNumber += 1


class Environment:
    def __init__(self, args, strict=True, bonus:bool=False):
        self.bonus = bonus
        self.lineCounter = LineCounter()
        self.santa = Santa()
        self.elves = [Elf(ID=i + 1) for i in range(args.NE)]
        self.rds = [Reindeer(ID=i + 1) for i in range(args.NR)]

        # shared variables for checking
        self.args = args
        self.strict = strict
        self.numElvesToHelp = 0
        self.reindeersHome = 0
        self.workshopOpen = True

    def end(self):
        # After all lines are read
        try:
            self.santaEnd(self.santa)
        except Exception as e:
            raise e

        for elf in self.elves:
            try:
                self.elfEnd(elf)
            except Exception as e:
                printWithLock(f'Elf {elf.ID} ended in wrong state')
                raise e

        for rd in self.rds:
            try:
                self.rdEnd(rd)
            except Exception as e:
                printWithLock(f'Reindeer {rd.ID} ended in wrong state')
                raise e

    def readLine(self, line):
        lineList = line.split(':')
        lineNum, actor, action = [item.strip() for item in lineList]

        self.lineCounter.read(lineNum)

        if actor == 'Santa':
            self.santaRead(self.santa, action)

        elif actor.startswith('Elf'):
            ID = int(actor.split()[1])

            if self.bonus:
                if ID > len(self.elves):
                    for i in range(len(self.elves), ID):
                        self.elves.append(Elf(i + 1))

            self.elfRead(self.elves[ID - 1], action)

        elif actor.startswith('RD'):
            ID = int(actor.split()[1])
            self.rdRead(self.rds[ID - 1], action)

        else:
            printWithLock(f'Wrong actor identifier: {actor}')
            raise

    def santaEnd(self, santa):
        if santa.state != Santa.State.GONE:
            printWithLock(f'Santa ended in state {santa.state}')
            raise

    def santaRead(self, santa, text):
        if santa.state == Santa.State.NOT_STARTED:
            if text == 'going to sleep':
                if self.numElvesToHelp != 0:
                    printWithLock(f'There are still {self.numElvesToHelp} elves in workshop, that didn\'t get help')
                    raise
                santa.state = Santa.State.SLEEPING
            else:
                printWithLock(f'Santa in state {santa.state} cannot do {text}')
                raise

        elif santa.state == Santa.State.SLEEPING:
            if text == 'helping elves':
                if self.numElvesToHelp != 0:
                    printWithLock('Santa did not yet help all the elves in previous helping cycle (or helped more than 3)')
                    raise
                if self.strict and self.reindeersHome == self.args.NR:
                    printWithLock('Santa cannot help elves, when all reindeers are home')
                    raise
                self.numElvesToHelp = 3
                santa.state = Santa.State.HELPING_ELVES
            elif text == 'closing workshop':
                if self.reindeersHome != self.args.NR:
                    printWithLock('Santa is closing workshop before all reindeers are home')
                    raise
                self.workshopOpen = False
                santa.state = Santa.State.HITCHING_RDS
            else:
                printWithLock(f'Santa in state {santa.state} cannot do {text}')
                raise

        elif santa.state == Santa.State.HELPING_ELVES:
            if text == 'going to sleep':
                if self.numElvesToHelp != 0:
                    printWithLock(f'Santa went to sleep, he still has {self.numElvesToHelp} elves in his workshop')
                    raise
                santa.state = Santa.State.SLEEPING
            else:
                printWithLock(f'Santa in state {santa.state} cannot do {text}')
                raise

        elif santa.state == Santa.State.HITCHING_RDS:
            if text == 'Christmas started':
                santa.state = Santa.State.GONE
            else:
                printWithLock(f'Santa in state {santa.state} cannot do {text}')
                raise

        elif santa.state == Santa.State.GONE:
            printWithLock(f'Santa in state {santa.state} cannot do {text}')
            raise

    def elfEnd(self, elf):
        if elf.state != Elf.State.ON_VACATION:
            printWithLock(f'Elf {elf.ID} ended in state {elf.state}')
            raise

    def elfRead(self, elf, text):
        if elf.state == Elf.State.NOT_STARTED:
            if text == 'started':
                elf.state = Elf.State.WORKING_ALONE
            else:
                printWithLock(f'Elf in state {elf.state} cannot do {text}')
                raise

        elif elf.state == Elf.State.WORKING_ALONE:
            if text == 'need help':
                elf.state = Elf.State.AWAITING_HELP
            else:
                printWithLock(f'Elf in state {elf.state} cannot do {text}')
                raise

        elif elf.state == Elf.State.AWAITING_HELP:
            if text == 'get help':
                if not self.workshopOpen:
                    printWithLock('Elf cannot get help after the workshop is closed')
                    raise
                if self.santa.state != Santa.State.HELPING_ELVES:
                    printWithLock(f'Santa cannot help an elf in state {self.santa.state}')
                    raise

                self.numElvesToHelp -= 1
                elf.state = Elf.State.WORKING_ALONE
            elif text == 'taking holidays':
                if self.workshopOpen:
                    printWithLock('Elf cannot go on vacation before the workshop closes')
                    raise
                elf.state = Elf.State.ON_VACATION
            else:
                printWithLock(f'Elf in state {elf.state} cannot do {text}')
                raise

        elif elf.state == Elf.State.ON_VACATION:
            printWithLock(f'Elf in state {elf.state} cannot do {text}')
            raise

    def rdEnd(self, rd):
        if rd.state != Reindeer.State.HITCHED:
            printWithLock(f'Reindeer {rd.ID} ended in state {rd.state}')
            raise

    def rdRead(self, rd, text):
        if rd.state == Reindeer.State.NOT_STARTED:
            if text == 'rstarted':
                rd.state = Reindeer.State.ON_VACATION
            else:
                printWithLock(f'Reindeer in state {rd.state} cannot do {text}')
                raise

        elif rd.state == Reindeer.State.ON_VACATION:
            if text == 'return home':
                self.reindeersHome += 1
                rd.state = Reindeer.State.BACK_HOME
            else:
                printWithLock(f'Reindeer in state {rd.state} cannot do {text}')
                raise

        elif rd.state == Reindeer.State.BACK_HOME:
            if text == 'get hitched':
                if self.workshopOpen:
                    printWithLock('Workshop must be closed, when a reindeer gets hitched')
                    raise
                if self.santa.state != Santa.State.HITCHING_RDS:
                    printWithLock(f'Santa cannot hitch a reindeer in state {self.santa.state}')
                    raise
                rd.state = Reindeer.State.HITCHED
            else:
                printWithLock(f'Reindeer in state {rd.state} cannot do {text}')
                raise

        elif rd.state == Reindeer.State.HITCHED:
            printWithLock(f'Reindeer in state {rd.state} cannot do {text}')
            raise


# things used for formatting
class fmt:
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[0;33m'
    DIM = '\e[2m'
    NOCOLOR = '\033[0m'
    TICK = '\u2713'
    CROSS = '\u2717'

class ProcessHolder:
    def __init__(self, args, timeout:Union[None, float, int]=None, workDir=".", bonus:bool=False):
        self.terminated = False
        self.args = args
        self.workDir = workDir
        self.timeout = timeout
        self.bonus = bonus
        self.process = None

        self.final_args = ["./proj2"]
        if bonus: self.final_args.append("-b")
        self.final_args.extend([str(args.NE), str(args.NR), str(args.TE), str(args.TR)])

    def run(self):
        self.process = subprocess.Popen(self.final_args, cwd=self.workDir, stderr=subprocess.PIPE)

        bonus_thread = None
        if self.bonus:
            bonus_thread = threading.Thread(target=self.usr_sig_sender)
            bonus_thread.start()

        try:
            _, err = self.process.communicate(timeout=self.timeout)
            self.terminated = True

        except subprocess.TimeoutExpired:
            self.process.terminate()
            self.terminated = True
            printWithLock(f'The program took longer than {self.timeout} seconds and has been terminated')
            if bonus_thread: bonus_thread.join()
            raise

        except KeyboardInterrupt as e:
            self.process.terminate()
            self.terminated = True
            printWithLock('The testing has been cancelled by the user')
            if bonus_thread: bonus_thread.join()
            raise e

        if self.process.returncode != 0:
            printWithLock(f'The tested program returned error\nError output: {err.decode("utf-8")[:-1]}')
            if bonus_thread: bonus_thread.join()
            raise

        if bonus_thread: bonus_thread.join()

    def usr_sig_sender(self):
        signal_countdown = 3
        while not self.terminated and signal_countdown > 0:
            try:
                time.sleep(random.random() * 0.05 + 0.001)
                self.process.send_signal(signal.SIGUSR1)
                signal_countdown -= 1
            except:
                pass

    # In case something bad happened on destroy of instance terminate process
    def __del__(self):
        if self.process is not None and not self.terminated:
            try:
                self.process.terminate()
            except:
                pass


def analyzeFile(file, args, strict=True, bonus:bool=False):
    # initialize the test environment with all the creatures
    env = Environment(args, strict, bonus)

    for lineNumber, line in enumerate(file.readlines()):
        try:
            env.readLine(line)

        except Exception as e:
            printWithLock(f'Illegal operation on line {lineNumber}:\n{line}')
            raise e

    env.end()


class Controller:
    def __init__(self, testedArguments=None, timeToRun=30, mute:bool=False):
        if testedArguments is None:
            testedArguments = [Arguments(20, 5, 0, 50)]

        self.testedArguments = testedArguments
        self.timeToRun = timeToRun
        self.startTime = perf_counter()
        self.testsRun = 0
        # list of arguments, that will be tested
        self.args = Arguments()

        self.mute = mute

    def nextRun(self):
        finished_part = (perf_counter() - self.startTime) / self.timeToRun
        if finished_part >= 1:
            return False

        index = int(finished_part * len(self.testedArguments))

        if self.args != self.testedArguments[index]:
            self.args = self.testedArguments[index]

            if not self.mute:
                printWithLock(f'Status: {int(finished_part * 100)}% done. {self.testsRun} tests have run. ' +
                              f'Testing: ./proj2 {self.args.NE} {self.args.NR} {self.args.TE} {self.args.TR}')

        self.testsRun += 1
        return True


def run_tests(testArgs, exec_time, timeout, strict, mute=False, workDir=".", bonus:bool=False):
    cont = Controller(testedArguments=testArgs, timeToRun=exec_time, mute=mute)

    try:
        while cont.nextRun():
            ProcessHolder(cont.args, timeout=timeout, workDir=workDir, bonus=bonus).run()
            with open(f'{workDir}/proj2.out', 'r') as file:
                analyzeFile(file, cont.args, strict=strict, bonus=bonus)

    except KeyboardInterrupt:
        printWithLock(fmt.YELLOW + fmt.CROSS + ' Test has been cancelled by the user' + fmt.NOCOLOR)
        raise

    except Exception:
        printWithLock(fmt.RED + fmt.CROSS + ' Tests failed' + fmt.NOCOLOR)
        raise

    printWithLock(fmt.GREEN + fmt.TICK + ' Tests passed' + fmt.NOCOLOR)


class Worker(multiprocessing.Process):
    def __init__(self, args, exec_time, timeout, strict, workdir, id, infinite, bonus:bool=False):
        super(Worker, self).__init__()
        self.daemon = True

        self.args = args
        self.exec_time = exec_time
        self.timeout = timeout
        self.strict = strict
        self.workdir = workdir
        self.id = id
        self.infinite = infinite
        self.bonus = bonus

    def run(self) -> None:
        printWithLock(fmt.GREEN + f'Process {self.id} started' + fmt.NOCOLOR)

        try:
            if self.infinite:
                test_counter = 0
                while True:
                    run_tests(self.args, self.exec_time, self.timeout, self.strict, True, self.workdir, self.bonus)
                    test_counter += 1

                    printWithLock(fmt.GREEN + f'Process {self.id} finished successfuly {test_counter} test cycles' + fmt.NOCOLOR)
            else:
                run_tests(self.args, self.exec_time, self.timeout, self.strict, True, self.workdir, self.bonus)

        except KeyboardInterrupt:
            printWithLock(fmt.YELLOW + fmt.CROSS + f' Test on process {self.id} has been cancelled by the user' + fmt.NOCOLOR)
            raise
        except:
            printWithLock(fmt.RED + fmt.CROSS + f' Process {self.id} failed a tests' + fmt.NOCOLOR)
            raise

        printWithLock(fmt.GREEN + fmt.TICK + f' Process {self.id} finished successfuly' + fmt.NOCOLOR)


class MultiprocessController:
    def __init__(self, args, exec_time, timeout, strict, num_of_threads, infinite, bonus:bool=False):
        self.args = args
        self.exec_time = exec_time
        self.timeout = timeout
        self.strict = strict
        self.infinite = infinite
        self.bonus = bonus

        self.num_of_threads = num_of_threads

        if os.path.exists(f"testing") and os.path.isdir(f"testing"):
            shutil.rmtree(f"testing", True)
        os.mkdir(f"testing")

        for i in range(self.num_of_threads):
            os.mkdir(f"testing/process_{i}")
            shutil.copy("./proj2", f"testing/process_{i}/proj2")

    def run(self):
        threads = []
        for i in range(self.num_of_threads):
            threads.append(Worker(self.args, self.exec_time, self.timeout, self.strict, f"testing/process_{i}", i, self.infinite, self.bonus))
            threads[i].start()

        for thread in threads:
            thread.join()


def main():
    if not os.path.exists("./proj2") or not os.path.isfile("./proj2"):
        print(fmt.RED + fmt.CROSS + ' Project binary not found' + fmt.NOCOLOR)
        return 1

    parser = argparse.ArgumentParser(description="Tester for IOS project2 2020/2021")
    parser.add_argument("-t", "--time", type=float, default=30,
                        help="how long the test should run in seconds (default: 30)")
    parser.add_argument("-s", "--strict", action="store_true",
                        help="add some extra rules, that should not be necessary")
    parser.add_argument("-T", "--timeout", type=float, default=None,
                        help="set timeout in seconds for detecting deadlock (default: None - no timeout)")
    parser.add_argument("-F", "--full", action="store_true", help="adds a few test cases with more extreme arguments")
    parser.add_argument("-i", "--infinite", action="store_true", help="runs tests in infinite loop")
    parser.add_argument("-p", "--processes", type=int, default=1,
                        help="number of testing processes program will run (default: 1)")
    parser.add_argument("-b", "--bonus", action="store_true", help="run tests of bonus")

    args = parser.parse_args()

    testedArguments = baseArguments
    if args.full: testedArguments.extend(extendedArguments)

    # comment this line if you want to see the python exception
    sys.stderr = open('/dev/null', 'w')

    if args.processes > 1:
        try:
            MultiprocessController(testedArguments, args.time, args.timeout, args.strict, args.processes, args.infinite, args.bonus).run()
            return 0
        except:
            return 1
    else:
        if args.infinite:
            loop_counter = 1
            while True:
                print(fmt.GREEN + f"Starting test loop {loop_counter}" + fmt.NOCOLOR)
                try:
                    run_tests(testedArguments, args.time, args.timeout, args.strict, bonus=args.bonus)
                except:
                    return 1
                print(fmt.GREEN + f"Test loop {loop_counter} finished\n" + fmt.NOCOLOR)
                loop_counter += 1

        else:
            try:
                run_tests(testedArguments, args.time, args.timeout, args.strict, bonus=args.bonus)
            except:
                return 1
            return 0


if __name__ == '__main__':
    sys.exit(main())
