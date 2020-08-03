import subprocess
import sys

if __name__ == "__main__":
    mode = "gso" if len(sys.argv) < 2 else sys.argv[1]
    homepath="/usr/local/google/home/yugant/github/gso_change/perf_envoy_gso/"
    # Run Envoy Server First 
    envoy_script = subprocess.Popen(["python3",
                                     homepath+"gso_benchmark/run_envoy.py",
                                     mode])
    quic_clients_script = subprocess.Popen(["python3",
                                            homepath+"gso_benchmark/run_quic_clients.py",
                                            str(envoy_script.pid)])