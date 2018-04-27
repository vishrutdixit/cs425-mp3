import sys
##################################################
# python3 create_config.py d_min d_max num_proc  #
#                                                #
# @arg d_min - minimum delay                     #
# @arg d_max - maximum delay                     #
# @arg num_proc - number of processes            #
##################################################
def main():
    delay1 = sys.argv[1]
    delay2 = sys.argv[2]
    num_processes = int(sys.argv[3])

    filename = "multicast.config"
    file = open(filename, "w")

    file.write(str(delay1) + " " + str(delay2) + "\n")
    for i in range(num_processes):
    	file.write(str(i)+ " 127.0.0.1 " + str(4250+i) + "\n")

if __name__ == "__main__":
   main()
