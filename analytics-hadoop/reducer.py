"""Sorted streaming sum; also safe as a combiner."""
import itertools
import sys


def reduce_lines(lines):
    pairs = (line.rstrip('\n').split('\t', 1) for line in lines)
    for key, group in itertools.groupby(pairs, key=lambda pair: pair[0]):
        yield key, sum(int(value) for _, value in group)


if __name__ == '__main__':
    for key, value in reduce_lines(sys.stdin):
        print(f'{key}\t{value}')
