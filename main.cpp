#include <cstring>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <fstream>
#include <fcntl.h>
#include <csignal>
#include <sys/wait.h>
#include <cassert>
#include <pwd.h>
#include <dirent.h>
#include <algorithm>
#include <sys/stat.h>

#define fo(i, a, b) for(int i=(a);i<=(b);++i)
#define STR_WRITE(fd, str) write(fd,str.data(),str.length())
#define CHARS_WRITE(fd, chars) write(fd,chars,strlen(chars))
using namespace std;
pid_t front_job = -1;
typedef vector<string> StrVec;
const size_t kBufferSize = 4096;
const size_t kMaxJobs = 4096;
const int kNull = open("/dev/null", O_WRONLY);
bool terminal_input;

enum kJobStatus {
    Running,
    Done,
    Stopped
};

struct Job {
    pid_t pgid;
    kJobStatus status;
    string command;

    Job() = default;

    Job(pid_t pid, kJobStatus status, string command) : pgid(pid), status(status), command(command) {}
} jobs[kMaxJobs];

string work_dir;
string user_name;
string host_name;
string home;
//vector<int> front_jobs;
int jobs_front = 1, jobs_back = 1;
char buf[kBufferSize];

StrVec split(const string &s, const string &delimiters = " \t") {
    StrVec res;
    string::size_type last_pos = s.find_first_not_of(delimiters, 0);
    string::size_type first_pos = s.find_first_of(delimiters, last_pos);
    while (string::npos != first_pos || string::npos != last_pos) {
        res.push_back(s.substr(last_pos, first_pos - last_pos));
        last_pos = s.find_first_not_of(delimiters, first_pos);
        first_pos = s.find_first_of(delimiters, last_pos);
    }
    return res;
}

void trim(string &s) {
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t") + 1);
}

void CheckBackground() {

}

void ExecFront() {
    assert(front_job > 0);
    if (jobs[front_job].status == Stopped) {
        kill(jobs[front_job].pgid, SIGCONT);
        jobs[front_job].status = Running;
    }

    assert(front_job > 0);
    while (front_job > 0 && !waitpid(-jobs[front_job].pgid, nullptr, WUNTRACED));
    if (front_job > 0) jobs[front_job].status = Done;
}

void MyEcho(const StrVec &args, int input_fd, int output_fd, int err_fd) {
    fo(i, 1, args.size() - 1) {
        STR_WRITE(output_fd, args[i]);
        if (i < args.size() - 1)
            CHARS_WRITE(output_fd, " ");
    }
    CHARS_WRITE(output_fd, "\n");
    cerr << getpid() << " " << getppid() << endl;
}

void MyExit(const StrVec &args, int input_fd, int output_fd, int err_fd) {
    exit(0);
}

//void MySleep(const StrVec &args, int input_fd, int output_fd, int err_fd) {
//    try {
//        int sec = stoi(args[1]);
//        sleep(sec);
//    } catch (...) {
//        CHARS_WRITE(err_fd, "Invalid argument of sleep command.\n");
//    }
//
//}
void MyExec(const StrVec &args, int input_fd, int output_fd, int err_fd) {
    char **s = new char *[args.size() + 1];
    fo(i, 0, args.size() - 1) {
        s[i] = new char[args[i].length() + 1];
        strcpy(s[i], args[i].data());
    }
    s[args.size()] = nullptr;
    dup2(input_fd, STDIN_FILENO);
    dup2(output_fd, STDOUT_FILENO);
    dup2(err_fd, STDERR_FILENO);
    execvp(s[0], s);
    auto msg = (string) "Execute " + args[0] + " failed: " + strerror(errno) + "\n";
    STR_WRITE(err_fd, msg);
    for (int i = 0; i < args.size(); ++i) {
        delete[] s[i];
    }
    delete[] s;
}

void MyCall(const StrVec &args, int input_fd, int output_fd, int err_fd) {
    if (!fork()) {
        MyExec(args, input_fd, output_fd, err_fd);
        exit(0);
    } else {
        wait(nullptr);
    }
}

void MyFg(const StrVec &args, int input_fd, int output_fd, int err_fd) {
    if (args.size() != 2) {
        CHARS_WRITE(err_fd, "Invalid argument of fg command.\n");
        return;
    }
    int job_id;
    try {
        job_id = stoi(args[1]);
    }
    catch (...) {
        CHARS_WRITE(err_fd, "Invalid job id of fg command.\n");
        return;
    }
    if (job_id < jobs_front || job_id >= jobs_back) {
        CHARS_WRITE(err_fd, "Invalid job id.\n");
        return;
    }
    assert(job_id > 0);
    if (jobs[job_id].status == Done) {
        CHARS_WRITE(err_fd, "Job has terminated.\n");
        return;
    }
//    if (jobs[job_id].status == Stopped) {
//        CHARS_WRITE(err_fd, "Job is stopped.\n");
//        return;
//    }
    front_job = job_id;
    if (terminal_input) {
        STR_WRITE(STDOUT_FILENO, jobs[job_id].command);
        CHARS_WRITE(STDOUT_FILENO, "\n");
    }
//    front_jobs.push_back(job_id);
//    cout<<"pushed:"<<job_id<<endl;
//    cout <<"##*"<<waitpid(-jobs[job_id].pgid, nullptr, 0)<<endl;
//    cout << "####" << waitpid(-front_job, nullptr, 0) << endl;
//    cout << "####" << waitpid(front_job + 1, nullptr, 0) << endl;
//    cout << "####" << strerror(errno) << endl;
//    cout << "##" << front_job << endl;
    ExecFront();
//        CHARS_WRITE(output_fd, "WAIT!");
//    }
//    while (!waitpid(-front_job, nullptr,0));
//    front_job = -1;
}

void MyBg(const StrVec &args, int input_fd, int output_fd, int err_fd) {
    if (args.size() != 2) {
        CHARS_WRITE(err_fd, "Invalid argument of bg command.\n");
        return;
    }
    int job_id;
    try {
        job_id = stoi(args[1]);
    }
    catch (...) {
        CHARS_WRITE(err_fd, "Invalid job id of bg command.\n");
        return;
    }
    if (job_id < jobs_front || job_id >= jobs_back) {
        CHARS_WRITE(err_fd, "Invalid job id.\n");
        return;
    }
    assert(job_id > 0);
    if (jobs[job_id].status == Done) {
        CHARS_WRITE(err_fd, "Job has terminated.\n");
        return;
    }
    if (jobs[job_id].status == Stopped) {
        jobs[job_id].status = Running;
        kill(jobs[job_id].pgid, SIGCONT);
    }
    if (terminal_input) {
        auto msg = (string) "[" + to_string(jobs_back - 1) + "]\tRunning\t\"" + jobs[job_id].command + "\"\t" + to_string(jobs[job_id].pgid) +
                   "\n";
        STR_WRITE(STDOUT_FILENO, msg);
    }
}

void MyCd(const StrVec &args, int input_fd, int output_fd, int err_fd) {
    if (args.size() != 2) {
        CHARS_WRITE(err_fd, "Invalid argument of cd command.\n");
        return;
    }
    string path = args[1] == "~" ? home : args[1];
    if (chdir(path.data()) == -1) {
        auto msg = (string) "Change directory failed: " + strerror(errno);
        STR_WRITE(err_fd, msg);
        return;
    }
    getcwd(buf, kBufferSize);
    path = buf;
    work_dir = split(path, "/").back();
    setenv("PWD", args[1].data(), 1);
}

void MyPwd(const StrVec &args, int input_fd, int output_fd, int err_fd) {
    getcwd(buf, kBufferSize);
    CHARS_WRITE(output_fd, buf);
}

void MyTime(const StrVec &args, int input_fd, int output_fd, int err_fd) {
    time_t raw_time;
    time(&raw_time);
    tm *tmp = localtime(&raw_time);
    strftime(buf, kBufferSize, "%Y-%m-%d %H:%M:%S", tmp);
    CHARS_WRITE(output_fd, buf);
}

void MyClr(const StrVec &args, int input_fd, int output_fd, int err_fd) {
    if (output_fd == STDOUT_FILENO) {
        printf("\033c");
    }
}

void MyDir(const StrVec &args, int input_fd, int output_fd, int err_fd) {
    if (args.size() != 2) {
        CHARS_WRITE(err_fd, "Invalid argument of dir command.\n");
        return;
    }
    DIR *dir;
    string path = args[1] == "~" ? home : args[1];
    if ((dir = opendir(path.data())) == nullptr) {
        auto msg = (string) "Open directory failed: " + strerror(errno);
        STR_WRITE(err_fd, msg);
        return;
    }
    StrVec files;
    dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        files.push_back(entry->d_name);
    }
    sort(files.begin(), files.end());
    for (const auto &file: files) {
        STR_WRITE(output_fd, file);
        CHARS_WRITE(output_fd, "\n");
    }
    closedir(dir);
}

void MySet(const StrVec &args, int input_fd, int output_fd, int err_fd) {
    if (args.size() > 3) {
        CHARS_WRITE(err_fd, "Too many argument of set command.\n");
        return;
    }
    if (args.size() == 1) {
        for (int i = 0; environ[i]; ++i) {
            CHARS_WRITE(output_fd, environ[i]);
            CHARS_WRITE(output_fd, "\n");
        }
    } else if (args.size() == 2) {
        CHARS_WRITE(output_fd, getenv(args[1].data()));
        CHARS_WRITE(output_fd, "\n");
    } else {
        if (setenv(args[1].data(), args[2].data(), 1) == -1) {
            auto msg = (string) "Set environment variable failed: " + strerror(errno);
            STR_WRITE(err_fd, msg);
        }
        CHARS_WRITE(output_fd, getenv(args[1].data()));
        CHARS_WRITE(output_fd, "\n");
    }
}

void MyUmask(const StrVec &args, int input_fd, int output_fd, int err_fd) {
    if (args.size() > 2) {
        CHARS_WRITE(err_fd, "Too many argument of umask command.\n");
        return;
    }
    if (args.size() == 1) {
        mode_t old_mask = umask(0);
        umask(old_mask);
        STR_WRITE(output_fd, to_string(old_mask));
        CHARS_WRITE(output_fd, "\n");
    } else {
        try {
            mode_t mask = stoi(args[1]);
            umask(mask);
            auto msg = (string) "New mask :" + to_string(mask) + "\n";
            STR_WRITE(output_fd, msg);
        }
        catch (...) {
            CHARS_WRITE(err_fd, "Mask should be an valid integer.\n");
            return;
        }
    }
}

int ExecTest(const StrVec &args) {
    if (args.size() == 3) {
        if (args[1] == "-d" || args[1] == "-f" || args[1] == "-r" || args[1] == "-s" || args[1] == "-w" || args[1] == "-x" || args[1] == "-b" || args[1] == "-c" || args[1] == "-e" ||
            args[1] == "-L") {
            struct stat file_status;
            if (lstat(args[2].data(), &file_status) == -1) return 0;
            if (args[1] == "-d") return S_ISDIR(file_status.st_mode);
            else if (args[1] == "-r") return !access(args[2].data(), R_OK);
            else if (args[1] == "-f") return S_ISREG(file_status.st_mode) && !S_ISDIR(file_status.st_mode);
            else if (args[1] == "-s") return file_status.st_size > 0;
            else if (args[1] == "-w") return !access(args[2].data(), W_OK);
            else if (args[1] == "-x") return !access(args[2].data(), X_OK);
            else if (args[1] == "-b") return S_ISBLK(file_status.st_mode);
            else if (args[1] == "-c") return S_ISCHR(file_status.st_mode);
            else if (args[1] == "-e") return 1;
            else if (args[1] == "-L") return S_ISLNK(file_status.st_mode);
        }

    }
}

void Mytest(const StrVec &args, int input_fd, int output_fd, int err_fd) {
    if (args.size() > 4 || args.size() < 3) {
        CHARS_WRITE(err_fd, "Invalid argument of test command.\n");
        return;
    }
    int res = ExecTest(args);

}

void ExecSingleCmd(const StrVec &args, int input_fd, int output_fd, int err_fd) {
    if (args[0] == "cd") {
        MyCd(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "pwd") {
        MyPwd(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "time") {
        MyTime(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "clr") {
        MyClr(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "dir") {
        MyDir(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "set") {
        MySet(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "bg") {
        MyBg(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "fg") {
        MyFg(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "jobs") {
//        MyJobs(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "exit") {
        MyExit(args, input_fd, output_fd, err_fd);
    } else {
        MyCall(args, input_fd, output_fd, err_fd);
    }
}

void ExecRedirect(const string &cmd_str, int input_fd = STDIN_FILENO, int output_fd = STDOUT_FILENO,
                  int err_fd = STDERR_FILENO) {
    StrVec elements = split(cmd_str);
    StrVec args;
    elements.push_back("");
    fo(i, 0, elements.size() - 2) {
        if (elements[i] == ">") {
            auto fd = open(elements[i + 1].data(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
            output_fd = fd;
            ++i;
        } else if (elements[i] == ">>") {
            auto fd = open(elements[i + 1].data(), O_WRONLY | O_APPEND | O_CREAT, 0666);
            output_fd = fd;
            ++i;
        } else if (elements[i] == "<") {
            auto fd = open(elements[i + 1].data(), O_RDONLY);
            input_fd = fd;
            ++i;
        } else if (elements[i] == "2>") {
            auto fd = open(elements[i + 1].data(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
            err_fd = fd;
            ++i;
        } else if (elements[i] == "2>>") {
            auto fd = open(elements[i + 1].data(), O_WRONLY | O_APPEND | O_CREAT, 0666);
            err_fd = fd;
            ++i;
        } else {
            args.emplace_back(elements[i]);
        }
    }
    ExecSingleCmd(args, input_fd, output_fd, err_fd);
}

int ExecCmdPipe(const string &cmd_str, int input_fd = STDIN_FILENO, int output_fd = STDOUT_FILENO, int err_fd = STDERR_FILENO) {
    cerr << getpid() << " " << getppid() << endl;
    StrVec cmds = split(cmd_str, "|");
    for (auto &o: cmds) trim(o);
    int fds[2];
    vector<pair<int, int>> pipe_fds;
    pid_t pgid = -1;
    fo(i, 0, (int) cmds.size() - 2) {
        if (pipe(fds) == -1) {
            auto msg = (string) "Create pipe failed: " + (string) strerror(errno);
            STR_WRITE(err_fd, msg);
            return -1;
        }
        auto pid = fork();
        if (pid == -1) {
            auto msg = (string) "Fork failed when execute pipe: " + strerror(errno);
            STR_WRITE(err_fd, msg);
            return -1;
        }
        if (pgid == -1) {
            pgid = pid;
        }
        setpgid(pid, pgid);
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            if (i) {
                ExecRedirect(cmds[i], pipe_fds.back().first, fds[1]);
            } else {
                ExecRedirect(cmds[i], input_fd, fds[1]);
            }
            exit(0);
        }
        pipe_fds.emplace_back(fds[0], fds[1]);;
    }
    auto pid = fork();
    if (pid == -1) {
        auto msg = (string) "Fork failed when execute pipe: " + strerror(errno);
        STR_WRITE(err_fd, msg);
        return -1;
    }
    if (pgid == -1) {
        pgid = pid;
    }
    setpgid(pid, pgid);
    bool child_flag = true;
    auto first_cmd = split(cmds.front()).front();
    if (cmds.size() == 1 && (first_cmd == "fg" || first_cmd == "bg" || first_cmd == "cd" || first_cmd == "exit" || first_cmd == "set")) {
        child_flag = false;
    }
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        if (cmds.size() > 1) {
            ExecRedirect(cmds.back(), pipe_fds.back().first, output_fd);
        } else if (child_flag) {
            ExecRedirect(cmds.back(), input_fd, output_fd);
        }
        exit(0);
    }
    if (!child_flag) {
        cout << "no child" << endl;
        ExecRedirect(cmds.back(), input_fd, output_fd);
    }
    return pgid;
}

void CheckJobs() {
    fo(i, jobs_front, jobs_back - 1)
        if (jobs[i].status == Running) {
            pid_t pid;
            while ((pid = waitpid(-jobs[i].pgid, nullptr, WNOHANG)) > 0);
            if (pid == -1 && errno == ECHILD) {
                jobs[i].status = Done;
                if (terminal_input) {
                    auto msg = (string) "[" + to_string(i) + "]\tDone\t\"" + jobs[i].command + "\"\t" + to_string(jobs[i].pgid) +
                               "\n";
                    STR_WRITE(STDOUT_FILENO, msg);
                }
            }
        }
    while (jobs_front < jobs_back && jobs[jobs_back - 1].status == Done) {
        --jobs_back;
    }
    if (jobs_front == jobs_back) {
        jobs_front = jobs_back = 1;
    }
}

void
ExecCmdBackground(const string &cmd_str, int input_fd = STDIN_FILENO, int output_fd = STDOUT_FILENO, int err_fd = STDERR_FILENO) {
    StrVec cmds = split(cmd_str, "&");
    fo(i, 0, (int) cmds.size() - 1) {
        trim(cmds[i]);
        if (jobs_back == kMaxJobs) {
            string msg = "Too many jobs";
            STR_WRITE(err_fd, msg);
        } else {
            auto pgid = ExecCmdPipe(cmds[i], input_fd, output_fd, err_fd);
            assert(jobs_back > 0);
            jobs[jobs_back].pgid = pgid;
            jobs[jobs_back].status = Running;
            jobs[jobs_back].command = cmds[i];
            jobs_back = jobs_back + 1;
            if (i == cmds.size() - 1 && split(cmd_str).back() != "&") {
                front_job = jobs_back - 1;
                ExecFront();
//                cout<<"$$$"<<waitpid(pgid, nullptr, 0)<<endl;
            } else {
                if (terminal_input) {
                    auto msg = (string) "[" + to_string(jobs_back - 1) + "]\tRunning\t\"" + cmds[i] + "\"\t" + to_string(pgid) +
                               "\n";
                    STR_WRITE(STDOUT_FILENO, msg);
                }
            }
//                while (!waitpid(-pgid, nullptr,0));

        }
    }
}

void ShowPrompt() {
    string msg = (string) "[" + user_name + "@" + host_name + " " + work_dir + "]$ ";
    STR_WRITE(STDOUT_FILENO, msg);
}

void
InputAndExec(int cmd_input_fd, int input_fd = STDIN_FILENO, int output_fd = STDOUT_FILENO, int err_fd = STDERR_FILENO) {
    char cmd_buf[kBufferSize];
    ssize_t cmd_len;
    string cmd_str;
    if (terminal_input) {
        ShowPrompt();
    }
    while ((cmd_len = read(cmd_input_fd, cmd_buf, kBufferSize)) > 0) {
        fo(i, 0, cmd_len - 1)
            if (cmd_buf[i] == '\n') {
                CheckJobs();
                if (!cmd_str.empty()) ExecCmdBackground(cmd_str);
//                int val;
////                assert(front_job>0);
//                while (~front_job && !(waitpid(-jobs[front_job].pgid, nullptr, WUNTRACED))){
//                    assert(front_job>0);
////                    cout<<"#$%"<<endl;
////                    cout<<val<<endl;
////                    sleep(1);
//                };
//                if (~front_job){
//                    jobs[front_job].status = Done;
//                    front_job = -1;
//                }
                cout << "end" << endl;
                cmd_str.clear();
                if (terminal_input) {
                    ShowPrompt();
                }
            } else
                cmd_str += cmd_buf[i];
    }
    if (cmd_len < 0) {
        auto msg = (string) "Read command error: " + strerror(errno);
        STR_WRITE(err_fd, msg);
    }
}

void SignalHandler(int signal) {
    if (signal == SIGINT) {
        CHARS_WRITE(STDOUT_FILENO, "\n");
        if (front_job == -1) return;
        assert(front_job > 0);
        kill(-jobs[front_job].pgid, SIGINT);
//        ShowPrompt();
    } else if (signal == SIGCHLD) {
        cout << "SHLD" << endl;
//        fo(i,jobs_front,jobs_back-1){
//            if (waitpid(-jobs[i].pgid,nullptr,WNOHANG)==-1){
//                jobs[i].status=Done;
//            };
//        }
    } else if (signal == SIGTSTP) {
        CHARS_WRITE(STDOUT_FILENO, "\n");
        if (front_job == -1) return;
        assert(front_job > 0);
        kill(jobs[front_job].pgid, SIGTSTP);
        if (terminal_input) {
            auto msg = (string) "[" + to_string(front_job) + "]\tStopped\t\"" + jobs[front_job].command + "\"\t" + to_string(jobs[front_job].pgid) +
                       "\n";
            STR_WRITE(STDOUT_FILENO, msg);
        }
        jobs[front_job].status = Stopped;
        front_job = -1;

    }
}

void Init() {
//    cout<<getenv("HOSTNAME")<<" "<<getenv("USER")<<" "<<getenv("PWD")<<endl;
    gethostname(buf, kBufferSize);
    host_name = buf;//getenv("HOSTNAME");
    getlogin_r(buf, kBufferSize);//getenv("USER");
    user_name = buf;
    getcwd(buf, kBufferSize);
    work_dir = buf;//getenv("PWD");
    work_dir = split(work_dir, "/").back();
    home = getpwuid(getuid())->pw_dir;
    if (readlink("/proc/self/exe", buf, kBufferSize) == -1) {
        CHARS_WRITE(STDOUT_FILENO, "Readlink error: ");
        CHARS_WRITE(STDOUT_FILENO, strerror(errno));
        CHARS_WRITE(STDOUT_FILENO, "\n");
    }
    if (setenv("shell", buf, 1) == -1) {
        CHARS_WRITE(STDOUT_FILENO, "Setenv shell error: ");
        CHARS_WRITE(STDOUT_FILENO, strerror(errno));
        CHARS_WRITE(STDOUT_FILENO, "\n");
    }
    signal(SIGINT, SignalHandler);
    signal(SIGTSTP, SignalHandler);
    signal(SIGCHLD, SignalHandler);
}

int main(int argc, char **argv) {
    Init();
    if (argc > 1) {
        terminal_input = false;
        fo(i, 1, argc - 1) {
            auto cmd_input_fd = open(argv[i], O_RDONLY);
            if (cmd_input_fd == -1) {
                auto msg = (string) "Open file " + argv[i] + " failed: " + strerror(errno) + "\n";
                STR_WRITE(STDERR_FILENO, msg);
            }
            InputAndExec(cmd_input_fd);
        }
    } else {
        terminal_input = true;
        InputAndExec(STDIN_FILENO);
    }
    return 0;
}
