import os
import sys
import shutil

from util.csma_queueing import do as do_csma

def dataset_path(i):
    return "./datasets/dataset{}".format(i)

def free_dataset():
    for i in range(1,1000):
        if not os.path.exists(dataset_path(i)):
            return i
    raise IndexError("You've made too many datasets, you goose!")

def collect_file(dataset, filepath):
    shutil.copy(filepath,dataset_path(dataset))

def generate_queue_data(dataset):
    store = os.getcwd()
    os.chdir(dataset_path(dataset))
    do_csma("csma.tr")
    os.chdir(store)

def main():
    # Find a free dataset slot to use
    slot = free_dataset()
    # Collect files
    os.mkdir(dataset_path(slot))
    collect = [
        "../ns-3.23/csma.tr",
        "../ns-3.23/simulationinfo.json",
        "network.layout",
        "schedule.dat",
        "gm.log",
        "lb.log",
        "analysis.ipynb",
        "mod_labels.dat",
        "migrations.dat",
        "groupsizes.dat",
        "losses.dat",
        "settings.py",
        "ecns.dat",
    ]
    map(lambda x: collect_file(slot,x), collect)
    generate_queue_data(slot)

if __name__ == "__main__":
    main()
