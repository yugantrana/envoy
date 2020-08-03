import subprocess
import os, signal
import sys
import time

class QuicClientConnections:
    def __init__(self, num_clients, run_envoy_pid):
        self.num_clients = num_clients
        self.run_envoy_pid = run_envoy_pid
        self.running_time = 0.0
    
    def spawn_quic_client(self):
        return subprocess.Popen([ "/google/data/ro/teams/quic/tools/quic_client",
                                  "--host=127.0.0.1",
                                  "--port=8009",
                                  "-disable_certificate_verification",
                                  "-quiet",
                                  "-num_requests=10",
                                  "www.random.com"])
        
    def run_and_terminate(self, granularity=1):
        quic_processes = []
        start_time = time.time()
        for i in range(self.num_clients):
            quic_processes.append(self.spawn_quic_client())
        
        done_procs = 0
        while(done_procs != self.num_clients):
            done_procs = 0
            for quic_proc in quic_processes:
                if quic_proc.poll() != None:
                    done_procs += 1
            print(done_procs, "completed")
            time.sleep(granularity)
        end_time = time.time()
        self.running_time = end_time - start_time

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Provide Envoy Script PID... Exiting!")
        exit(1)
    run_envoy_pid = int(sys.argv[1])
    num_clients = 50 if len(sys.argv) < 3 else sys.argv[2]
    quic_connections =  QuicClientConnections(num_clients, run_envoy_pid)
    quic_connections.run_and_terminate()
    print("Final Stats while running", num_clients,"Quic Connections",
          "\n  Running Time:\t", quic_connections.running_time)
    # Send SIGINT to Envoy Server When all Clients are done 
    os.kill(quic_connections.run_envoy_pid, signal.SIGINT)