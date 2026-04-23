# OS Jackfruit Container Runtime

---

## 1. Team Information

- **Jiya Datta Banik** – PES1UG24CS202  
- **Sai Lavanya Kanchinadham** – PES1UG24CS214  

---

## 2. Build, Load and Run Instructions

###  Build the project
```bash
make clean
make
```
   
### Setup root filesystem (alpine)
   ```bash
   mkdir rootfs-base
   tar -xzf alpine-minirootfs-3.19.9-x86_64.tar.gz -C rootfs-base
   ```

### Start supervisor
   ```bash 
    sudo ./engine supervisor ./rootfs-base
   ```

### Check running containers
```bash
ps aux | grep sh
```

### View container metadata
```bash
   ./engine ps
```
   
### Run workload (outside container)
```bash
   ./memory_hog
   ./cpu_hog
```
   
 ### Stop processes
 ```bash
   pkill -9 memory_hog
   pkill -9 cpu_hog
   ```
 ### Kernel logs
 ```bash
   dmesg | tail
```
   
 ### Cleanup
 ```bash
   sudo rmmod monitor
```

## 3. Demo with screenshots
1.

  ![s1](https://github.com/user-attachments/assets/7e3934c9-b8a3-464f-8e14-fc5e05c7cf30)

two containers (alpha and beta) running under one supervisor process


2.
 ![s2](https://github.com/user-attachments/assets/ad487e4b-59db-4532-8763-e0e1c194a577)

output of engine ps showing container IDs and PIDs


3.
 ![s3](https://github.com/user-attachments/assets/d0da0a38-b25c-4c18-8642-357404d857f9)

containers generating logs concurrently demonstrating producer behaviour in logging pipeline


4.
![s4](https://github.com/user-attachments/assets/bceb0132-671f-4144-80a9-18fbe4bf80e8)

CLI issues a request and receives a response from supervisor


5.
![s5](https://github.com/user-attachments/assets/cc316d33-bf8b-427f-b605-d1b0dd93bbcc)

kernel logs show warning when container exceeds soft memory limit 


6.
![s6](https://github.com/user-attachments/assets/19506b52-26ef-483f-8ba9-2b0e6ffc5f8a)

kernel logs show container termination after exceeding hard limit


7.
<img width="624" height="370" alt="ss" src="https://github.com/user-attachments/assets/30a86cf7-686c-47ae-8456-98b782cd03eb" />


Three CPU-bound processes pinned to the same CPU core using taskset, with one process assigned a lower priority via renice, demonstrating priority-based CPU scheduling


8.
  ![s8](https://github.com/user-attachments/assets/bbf8a587-7424-48a8-b29a-988d51a68f61)

processes are terminated and no zombie processes remain (ps aux verification)


## Engineering Analysis

### Namespace Isolation

The project uses Linux namespaces (PID, UTS, mount) to isolate containers.
Each container runs with its own process tree and filesystem view, ensuring separation from the host system.

### Supervisor Architecture

A single supervisor process is responsible for:

spawning containers

tracking metadata

coordinating execution

This design centralizes control and simplifies management.

### Kernel Monitoring

A kernel module monitors memory usage of container processes.
It periodically checks RSS values and enforces:

soft limits (warnings)

hard limits (process termination)


### IPC Mechanism

Communication between CLI and supervisor is done using a control interface.
This allows commands like ps to query container state.

### Scheduling Behavior

Multiple CPU-intensive workloads demonstrate how Linux distributes CPU time fairly among processes using its scheduler.

## Design Tradeoffs 
### Namespace Design
Choice: Use Linux namespaces

Tradeoff: More complexity in setup

Justification: Provides strong isolation

### Supervisor Model
Choice: Single supervisor process

Tradeoff: Single point of failure

Justification: Easier coordination and debugging

### Logging System
Choice: Bounded buffer logging

Tradeoff: Possible log loss under heavy load

Justification: Prevents unbounded memory growth

### Kernel Module Monitoring
Choice: Kernel-level monitoring

Tradeoff: Risk of system instability if incorrect

Justification: Direct access to process memory metrics

### Scheduling Experiment

Choice: CPU hog processes

Tradeoff: Artificial workload

Justification: Clearly demonstrates scheduler behavior

## Scheduler Experiment Results 


### Observed Output

Three CPU-bound processes were pinned to the same CPU core using `taskset`, and one process was assigned a lower priority using `renice`.
<img width="624" height="370" alt="ss" src="https://github.com/user-attachments/assets/19d3a681-4674-4da4-8704-62c02f66af52" />


From the observed output:

- Two processes (default priority) used ~47% CPU each  
- One process (lower priority, NI=10) used only ~5% CPU  

---

### Analysis

- All processes were competing for the same CPU core  
- The Linux scheduler distributed CPU time based on priority  
- Higher priority processes received significantly more CPU time  
- The lower priority process was still scheduled, but less frequently  

---

### Key Observations

- CPU scheduling is **priority-aware**, not strictly equal  
- Nice values directly influence CPU allocation  
- Lower priority processes are not starved, but receive reduced CPU time  

---

### Conclusion

This experiment demonstrates that the Linux scheduler:
- respects process priority (nice values)  
- allocates CPU time unevenly when priorities differ  
- ensures fairness while still prioritizing higher-priority tasks  





   



