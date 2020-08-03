import psutil
import subprocess
import time
import signal, os
import sys

class EnvoyUdpWriterBenchmark:
    def __init__(self, writer="gso",
                 homepath="/usr/local/google/home/yugant/github/gso_change/perf_envoy_gso/"):
        self.writer = writer
        self.homepath = homepath
        self.conf_file = ""
        self.log_file = ""
        self.envoy_process = None
        self.quic_pids = []
        self.max_cpu = 0.0
        self.avg_cpu = 0.0

    def spawn_envoy(self):
        if self.envoy_process != None:
            print("Envoy Process Already Spawned with Pid", self.envoy_process.pid)
            return        
        self.conf_file = "configs/"+self.writer+"_quic_google_com_proxy.v2.yaml"
        self.log_file = "gso_benchmark/"+self.writer+"_benchmark.log"
        self.envoy_process = subprocess.Popen([self.homepath+"bazel-bin/source/exe/envoy-static-gso", 
                                               "--concurrency 1", 
                                               "--disable-hot-restart",
                                               "-c "+ self.homepath + self.conf_file,
                                               "--log-path " + self.homepath + self.log_file])
        print("Spawned Envoy Server with",self.writer,"writer",
              "\n  PID:", self.envoy_process.pid,
              "\n  ConfFile:", self.conf_file,
              "\n  LogFile:", self.log_file)

    def monitor_envoy(self, granularity=1):
        if self.envoy_process == None:
            print ("Spawn Envoy First!")
            return 
        print("Monitoring Envoy CPU Usage for Envoy", self.envoy_process.pid,
              "\n  Press Ctrl-C to Stop...")
        total_non_idle_cpu = 0.0
        non_idle_runs = 0
        psutil_proc = psutil.Process(self.envoy_process.pid)
        psutil_proc.cpu_percent()
        while(True):              # Very Long Time
            current_cpu = psutil_proc.cpu_percent()
            if current_cpu > self.max_cpu:
                self.max_cpu = current_cpu
            if current_cpu > 1.0:
                non_idle_runs +=1
                total_non_idle_cpu += current_cpu
                self.avg_cpu = total_non_idle_cpu/non_idle_runs
            psutil_proc.cpu_percent()
            time.sleep(granularity)

# global variables
global benchmark 

def signal_handler(sig, frame):
    print("")
    print("Final Stats for Envoy Server with",benchmark.writer,"writer",
          "\n  PID:\t", benchmark.envoy_process.pid,
          "\n  MaxCPU:", benchmark.max_cpu,
          "\n  AvgCPU:", benchmark.avg_cpu)
    os.killpg(os.getpgid(benchmark.envoy_process.pid), signal.SIGTERM)

if __name__ == "__main__":
    print("Script PID is :", os.getpid())
    writer = "gso" if len(sys.argv) < 2 else sys.argv[1]
    signal.signal(signal.SIGINT, signal_handler)
    benchmark = EnvoyUdpWriterBenchmark(writer)
    benchmark.spawn_envoy()
    # store global
    benchmark.monitor_envoy()

    