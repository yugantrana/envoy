# GSO BenchMark Testing Scripts
- run_quic_clients.py
    > argv[1]: [Num of quic clients], 50 if none given -> `N`
    - Spawns `N` QUIC clients each making 10 requests to envoy in parallel
    - Waits till all clients are served,  and then displays the total time needed to serve all QUIC.

- run_envoy.py {Used via run_benchmark.py}
    > argv[1]: ["gso" or "default"], to spawn envoy with default or gso writer. "gso" if none given.
    - Spawns an Envoy Server using subprocess.Popen.
    - Monitors the non-ideal CPU utilization of the envoy server
    - On receiving SIGINT (Ctrl-C), kills the envoy server process and displays AvgCPU utiliztion and Max CPU utilization stats

- run_quic_clients_with_envoy.py {Used via run_benchmark.py}
    > argv[1]: NON-OPTIONAL : Envoy Server Script PID, pid of the run_envoy.py script running.
    > argv[2]: [Num of quic clients], 50 if none given -> `N`
    - Similar Functionality as run_quic_clients.
    - Also, maintains an envoy server script pid, and sends a SIGINT to the envoy_script as soon as all clients are served

- run_benchmark.py
    > argv[1]: ["gso" or "default"], to spawn envoy with default or gso writer. "gso" if none given.
    - runs an envoy server using run_envoy.py
    - runs quic_clients using run_quic_clients_with_envoy.py
    - Final output will show Max and Avg CPU util of the envoy server on completion.

# Methods For Testing Performance
- Monitoring psutil CPU utilization for the Envoy Process, run from `../`

    `python3 gso_benchmark/run_benchmark.py "gso"`
    
    `python3 gso/benchmark/run_benchmark.py "default"`

- CPU-Profiling using PPROF
    1. Run Envoy Server By Saving CPU profile as
       
       For Default:
       `CPUPROFILE=/tmp/default_run.cpuprof ../bazel-bin/source/exe/envoy-static-gso --concurrency 1 --disable-hot-restart -c ../configs/default_quic_google_com_proxy.v2.yaml`
       
       For Gso:
       `CPUPROFILE=/tmp/gso_run.cpuprof ../bazel-bin/source/exe/envoy-static-gso --concurrency 1 --disable-hot-restart -c ../configs/gso_quic_google_com_proxy.v2.yaml`
    
    2. `python3 run_quic_clients.py`, from a separate terminal
    
    3. Once all quic clients are served, kill the envoy server and analyze the .cpuprof

       For Default:
       `pprof -text bazel-bin/source/exe/envoy-static-gso /tmp/default_run.cpuprof | less`

       For Gso:
       `pprof -text bazel-bin/source/exe/envoy-static-gso /tmp/gso_run.cpuprof | less`
 
 # Pre-Requisites
 - You would like to modify `../configs/default_quic_google_com_proxy.v2.yaml` and `../configs/gso_quic_google_com_proxy.v2.yaml`
    - Changing `/usr/local/google/home/yugant/github/gso_change/perf_envoy_gso/` to path to your repository.
    - Changing `filename: "/usr/local/google/home/yugant/getfile/myfile.log"` to path of the file that you want to return as response from server.

 - `bazel build //source/exe:envoy-static-gso -c opt`   

