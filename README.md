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
   
1. ![s1](https://github.com/user-attachments/assets/072f4f5b-29f9-4735-a8aa-28aac68c15a1)
two containers (alpha and beta) running under one supervisor process


2. ![s2](https://github.com/user-attachments/assets/fd2e7f53-0d16-4fcd-9442-19700ae5044c)
output of engine ps showing container IDs and PIDs


3. ![s3](https://github.com/user-attachments/assets/997366bc-210b-41d5-bd8a-fd732a4b7b8f)
containers generating logs concurrently demonstrating producer behaviour in logging pipeline


4. ![s4](https://github.com/user-attachments/assets/385da516-57e5-48c9-9da8-6432fee3aa1f)
CLI issues a request and receives a response from supervisor


5.![s5](https://github.com/user-attachments/assets/410726d0-dc52-4d41-b1a4-faa0cef1c0ab)
kernel logs show warning when container exceeds soft memory limit 


6.![s6](https://github.com/user-attachments/assets/b67d795f-1013-42d3-ad5e-c42f43b15981)
kernel logs show container termination after exceeding hard limit


7.![s7](https://github.com/user-attachments/assets/2336976a-836c-40f4-bda7-4497d0c7c51e)
CPU-intensive workloads (cpu_hog) shows observable CPU usage differences 


8. ![s8](https://github.com/user-attachments/assets/21df6fc6-02db-409b-a582-02d6bea9b33e)
processes are terminated and no zombie processes remain (ps aux verification)


## Engineering Analysis




   



