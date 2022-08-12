#include <cstring> //std::string类所需头文件
#include <iostream>
#include <vector> //std::vector类所需头文件
#include <unistd.h> //read write等系统调用所需头文件
#include <fcntl.h> //文件操作所需头文件
#include <sys/wait.h> //wait等系统调用所需头文件
#include <cassert>
#include <pwd.h> //getpwuid等系统调用所需头文件
#include <dirent.h> //opendir等系统调用所需头文件
#include <algorithm> //std::sort排序所需头文件
#include <sys/stat.h> //stat等系统调用所需头文件

#define fo(i, a, b) for(int i=(a);i<=(b);++i) //简化for循环
#define STR_WRITE(fd, str) write(fd,str.data(),str.length()) //输出string到指定fd
#define CHARS_WRITE(fd, chars) write(fd,chars,strlen(chars)) //输出char*到指定fd
using namespace std;
pid_t front_job = -1; //前台任务组id
typedef vector<string> StrVec;
const size_t kBufferSize = 4096; //缓冲区大小
const size_t kMaxJobs = 4096; //最大任务数
const int kNull = open("/dev/null", O_WRONLY); //空文件描述符
bool terminal_input; //是否从终端输入
enum kJobStatus {  //任务状态
    Running, //运行中
    Done, //已结束
    Stopped //已停止
};
StrVec kStatusStr{"Running", "Done", "Stopped"}; //任务状态对应字符串
struct Job { //任务结构体
    pid_t pgid; //任务组id
    kJobStatus status; //任务状态
    string command; //任务命令字符串
    Job() = default; //默认构造函数

    Job(pid_t pid, kJobStatus status, string command) : pgid(pid), status(status), command(command) {}
} jobs[kMaxJobs]; //任务队列

string work_dir; //工作目录
string user_name; //用户名
string host_name; //主机名
string home; //用户主目录
int jobs_front = 1, jobs_back = 1; //任务队列首尾
char buf[kBufferSize]; //缓冲区
StrVec split(const string &s, const string &delimiters = " \t") { //用分割符集合中的任意一个字符分割字符串
    StrVec res;
    string::size_type last_pos = s.find_first_not_of(delimiters, 0); //获取第一个不是分割符的位置
    string::size_type first_pos = s.find_first_of(delimiters, last_pos); //获取第一个分割符的位置
    while (string::npos != first_pos || string::npos != last_pos) { //如果二者都存在
        res.push_back(s.substr(last_pos, first_pos - last_pos)); //添加分割后的子串到结果中
        last_pos = s.find_first_not_of(delimiters, first_pos); //获取下一个不是分割符的位置
        first_pos = s.find_first_of(delimiters, last_pos); //获取下一个分割符的位置
    }
    return res; //返回结果
}
void trim(string &s) { //去除字符串前后的空白字符
    s.erase(0, s.find_first_not_of(" \t")); //去除前面的空白字符
    s.erase(s.find_last_not_of(" \t") + 1); //去除后面的空白字符
}
string VariableParse(string str) { //解析字符串中的变量
    string val;
    auto first_pos = str.find("${"); //获取第一个变量的位置
    while (first_pos != string::npos) { //还有待解析变量
        auto last_pos = str.find('}', first_pos); //获取变量的结束位置
        if (last_pos == string::npos) {
            break; //没有找到结束位置，退出循环
        }
        auto name = str.substr(first_pos + 2, last_pos - first_pos - 2); //获取变量名
        if (getenv(name.data())) { //如果变量存在
            str.replace(first_pos, last_pos - first_pos + 1, getenv(name.data())); //替换变量
        }
        first_pos = str.find("${", last_pos); //获取下一个变量的位置
    }
    return str; //返回解析后的字符串
}
void ExecFront() { //执行前台任务
    if (jobs[front_job].status == Stopped) { //如果任务已停止
        kill(jobs[front_job].pgid, SIGCONT); //发送恢复信号
        jobs[front_job].status = Running; //设置任务状态为运行中
    }
    while (front_job > 0 && ~waitpid(-jobs[front_job].pgid, nullptr, WUNTRACED)); //等待前台任务组结束
    if (front_job > 0) jobs[front_job].status = Done; //设置任务状态为已结束
}
void MyEcho(const StrVec &args, int input_fd, int output_fd, int err_fd) { //执行echo命令
    fo(i, 1, args.size() - 1) {
        STR_WRITE(output_fd, VariableParse(args[i])); //输出解析后参数
        if (i < args.size() - 1)
            CHARS_WRITE(output_fd, " "); //输出空格
    }
    CHARS_WRITE(output_fd, "\n"); //输出换行符
}
void MyExit(const StrVec &args, int input_fd, int output_fd, int err_fd) { //执行exit命令
    if (args.size() > 2) { //如果参数数量大于2
        CHARS_WRITE(err_fd, "Too many arguments for exit command.\n");
        return; //退出
    }
    exit(0); //结束当前进程
}
void MyExec(const StrVec &args, int input_fd, int output_fd, int err_fd) { //执行exec命令
    char **s = new char *[args.size() + 1]; //分配参数数组
    fo(i, 0, args.size() - 1) {
        s[i] = new char[args[i].length() + 1];
        strcpy(s[i], args[i].data()); //复制参数到字符数组
    }
    s[args.size()] = nullptr; //结尾置空
    dup2(input_fd, STDIN_FILENO); //输入重定向
    dup2(output_fd, STDOUT_FILENO); //输出重定向
    dup2(err_fd, STDERR_FILENO); //错误输出重定向
    close(4);
    close(5);
//    close(STDIN_FILENO);
//    close(output_fd);
    execvp(s[0], s);
    auto msg = (string) "Execute " + args[0] + " failed: " + strerror(errno) + "\n";
    STR_WRITE(err_fd, msg); //执行到此处说明执行失败
    for (int i = 0; i < args.size(); ++i) {
        delete[] s[i]; //释放参数数组
    }
    delete[] s; //释放参数数组
}
void MyCall(const StrVec &args, int input_fd, int output_fd, int err_fd) { //执行外部命令
//    pid_t pgid=getpgid(0); //获取当前进程的进程组id
//    if (!fork()) { //新建子进程执行外部命令
//        setpgid(0, pgid); //设置进程组id
//        MyExec(args, input_fd, output_fd, err_fd);
//        exit(0);
//    } else{
//
//        close(input_fd);
//        close(STDIN_FILENO);
////        close(STDOUT_FILENO);
//        waitpid(-1,nullptr,0); //等待子进程结束
//        cerr<<"ended"<<endl;
//    }
    MyExec(args, input_fd, output_fd, err_fd);
}

void MyFg(const StrVec &args, int input_fd, int output_fd, int err_fd) { //把后台任务调到前台
    if (args.size() != 2) { //如果参数数量不为2
        CHARS_WRITE(err_fd, "Invalid argument of fg command.\n");
        return; //退出
    }
    int job_id; //任务id
    try {
        job_id = stoi(args[1]); //将参数转换为整数
    }
    catch (...) { //如果转换失败
        CHARS_WRITE(err_fd, "Invalid job id of fg command.\n");
        return; //退出
    }
    if (job_id < jobs_front || job_id >= jobs_back) { //如果任务id超出范围
        CHARS_WRITE(err_fd, "Invalid job id.\n");
        return; //退出
    }
    if (jobs[job_id].status == Done) { //如果任务已结束
        CHARS_WRITE(err_fd, "Job has terminated.\n");
        return; //退出
    }
    front_job = job_id; //设置前台任务为当前任务
    if (terminal_input) { //如果是终端输入
        STR_WRITE(STDOUT_FILENO, jobs[job_id].command); //输出任务命令
        CHARS_WRITE(STDOUT_FILENO, "\n");
    }
    ExecFront(); //执行前台任务
}

void MyBg(const StrVec &args, int input_fd, int output_fd, int err_fd) { //把前台任务调到后台
    if (args.size() != 2) { //如果参数数量不为2
        CHARS_WRITE(err_fd, "Invalid argument of bg command.\n");
        return; //退出
    }
    int job_id; //任务id
    try {
        job_id = stoi(args[1]); //将参数转换为整数
    }
    catch (...) { //如果转换失败
        CHARS_WRITE(err_fd, "Invalid job id of bg command.\n");
        return; //退出
    }
    if (job_id < jobs_front || job_id >= jobs_back) { //如果任务id超出范围
        CHARS_WRITE(err_fd, "Invalid job id.\n");
        return; //退出
    }
    if (jobs[job_id].status == Done) { //如果任务已结束
        CHARS_WRITE(err_fd, "Job has terminated.\n");
        return; //退出
    }
    if (jobs[job_id].status == Stopped) { //如果任务已停止
        jobs[job_id].status = Running; //改为运行中
        kill(jobs[job_id].pgid, SIGCONT); //发送恢复信号
    }
    if (terminal_input) { //如果是终端输入
        auto msg = (string) "[" + to_string(jobs_back - 1) + "]\tRunning\t\"" + jobs[job_id].command + "\"\t" + to_string(jobs[job_id].pgid) + "\n"; //输出信息
        STR_WRITE(STDOUT_FILENO, msg);
    }
}
void MyCd(const StrVec &args, int input_fd, int output_fd, int err_fd) { //改变当前工作目录
    if (args.size() != 2) { //如果参数数量不为2
        CHARS_WRITE(err_fd, "Invalid argument of cd command.\n");
        return; //退出
    }
    string path = args[1] == "~" ? home : args[1]; //判断是否为~
    if (chdir(path.data()) == -1) { //尝试改变工作目录，如果失败
        auto msg = (string) "Change directory failed: " + strerror(errno); //输出错误信息
        STR_WRITE(err_fd, msg);
        return; //退出
    }
    getcwd(buf, kBufferSize); //获取当前工作目录
    path = buf;
    work_dir = split(path, "/").back(); //设置当前工作目录
    setenv("PWD", args[1].data(), 1); //设置环境变量PWD
}
void MyPwd(const StrVec &args, int input_fd, int output_fd, int err_fd) { //输出当前工作目录
    if (args.size() > 1) { //如果参数数量大于1
        CHARS_WRITE(err_fd, "Too many arguments for pwd command.\n");
        return; //退出
    }
    getcwd(buf, kBufferSize); //获取当前工作目录
    CHARS_WRITE(output_fd, buf); //输出当前工作目录
}
void MyTime(const StrVec &args, int input_fd, int output_fd, int err_fd) { //输出当前时间
    if (args.size() > 1) { //如果参数数量大于1
        CHARS_WRITE(err_fd, "Too many arguments for time command.\n");
        return; //退出
    }
    time_t raw_time;
    time(&raw_time); //获取当前时间
    tm *tmp = localtime(&raw_time);
    strftime(buf, kBufferSize, "%Y-%m-%d %H:%M:%S", tmp); //格式化为字符串
    CHARS_WRITE(output_fd, buf); //输出时间
}
void MyClr(const StrVec &args, int input_fd, int output_fd, int err_fd) { //清屏
    if (args.size() > 1) { //如果参数数量大于1
        CHARS_WRITE(err_fd, "Too many arguments for clr command.\n");
        return; //退出
    }
    if (output_fd == STDOUT_FILENO) { //如果是标准输出
        printf("\033c"); //清屏
    }
}
void MyDir(const StrVec &args, int input_fd, int output_fd, int err_fd) { //输出当前目录下的文件
    if (args.size() != 2) { //如果参数数量不为2
        CHARS_WRITE(err_fd, "Invalid argument of dir command.\n");
        return; //退出
    }
    DIR *dir;
    string path = args[1] == "~" ? home : args[1]; //判断是否为~
    if ((dir = opendir(path.data())) == nullptr) { //尝试打开目录，如果失败
        auto msg = (string) "Open directory failed: " + strerror(errno);
        STR_WRITE(err_fd, msg);
        return; //退出
    }
    StrVec files;
    dirent *entry;
    while ((entry = readdir(dir)) != nullptr) { //循环读取目录中内容
        if (entry->d_name[0] == '.') continue; //如果是隐藏文件，跳过
        files.push_back(entry->d_name); //将文件名添加到文件名列表中
    }
    sort(files.begin(), files.end()); //排序文件名
    for (const auto &file: files) {
        STR_WRITE(output_fd, file); //输出文件名
        CHARS_WRITE(output_fd, "\n");
    }
    closedir(dir); //关闭目录
}
void MySet(const StrVec &args, int input_fd, int output_fd, int err_fd) { //设置环境变量
    if (args.size() > 3) { //如果参数数量大于3
        CHARS_WRITE(err_fd, "Too many arguments for set command.\n");
        return; //退出
    }
    if (args.size() == 1) { //如果参数数量为1，输出所有环境变量
        for (int i = 0; environ[i]; ++i) { //循环输出环境变量
            CHARS_WRITE(output_fd, environ[i]);
            CHARS_WRITE(output_fd, "\n");
        }
    } else if (args.size() == 2) { //如果参数数量为2，输出指定环境变量
        CHARS_WRITE(output_fd, getenv(args[1].data())); //输出环境变量
        CHARS_WRITE(output_fd, "\n");
    } else { //如果参数数量为3，设置环境变量
        if (setenv(args[1].data(), args[2].data(), 1) == -1) { //尝试设置环境变量，如果失败
            auto msg = (string) "Set environment variable failed: " + strerror(errno);
            STR_WRITE(err_fd, msg);
        }
    }
}
void MyUmask(const StrVec &args, int input_fd, int output_fd, int err_fd) { //设置umask
    if (args.size() > 2) { //如果参数数量大于2
        CHARS_WRITE(err_fd, "Too many arguments for umask command.\n");
        return;
    }
    if (args.size() == 1) { //如果参数数量为1，输出当前umask
        mode_t old_mask = umask(0); //获取当前umask
        umask(old_mask); //恢复umask
        STR_WRITE(output_fd, to_string(old_mask)); //输出umask
        CHARS_WRITE(output_fd, "\n");
    } else { //如果参数数量为2，设置umask
        try {
            mode_t mask = stoi(args[1]); //尝试转换为整数
            umask(mask); //设置umask
            auto msg = (string) "New mask :" + to_string(mask) + "\n"; //输出新umask
            STR_WRITE(output_fd, msg);
        }
        catch (...) { //如果转换失败
            CHARS_WRITE(err_fd, "Mask should be an valid integer.\n");
            return; //退出
        }
    }
}
int ExecTest(const StrVec &args) { //test命令
    if (args.size() == 3) { //如果参数数量为3
        if (args[1] == "-d" || args[1] == "-f" || args[1] == "-r" || args[1] == "-s" || args[1] == "-w" || args[1] == "-x" || args[1] == "-b" || args[1] == "-c" || args[1] == "-e" || args[1] == "-L") {
            //如果是文件相关
            struct stat file_status;
            auto file_path = VariableParse(args[2]); //解析文件路径
            if (lstat(file_path.data(), &file_status) == -1) return 0; //尝试获取文件状态
            if (args[1] == "-d") return S_ISDIR(file_status.st_mode); //如果是-d，判断是否是目录
            else if (args[1] == "-r") return !access(file_path.data(), R_OK); //如果是-r，判断是否可读
            else if (args[1] == "-f") return S_ISREG(file_status.st_mode) && !S_ISDIR(file_status.st_mode); //如果是-f，判断是否是普通文件
            else if (args[1] == "-s") return !!file_status.st_size; //如果是-s，判断长度是否非0
            else if (args[1] == "-w") return !access(file_path.data(), W_OK); //如果是-w，判断是否可写
            else if (args[1] == "-x") return !access(file_path.data(), X_OK); //如果是-x，判断是否可执行
            else if (args[1] == "-b") return S_ISBLK(file_status.st_mode); //如果是-b，判断是否是块设备
            else if (args[1] == "-c") return S_ISCHR(file_status.st_mode); //如果是-c，判断是否是字符设备
            else if (args[1] == "-e") return 1; //如果是-e，判断文件是否存在
            else if (args[1] == "-L") return S_ISLNK(file_status.st_mode); //如果是-L，判断是否是符号链接
        }
        if (args[1] == "-n" || args[1] == "-z") { //如果是字符串相关
            auto str = VariableParse(args[2]); //解析字符串中的变量
            if (args[1] == "-n") return !str.empty(); //如果是-n，判断是否非空
            if (args[1] == "-z") return str.empty(); //如果是-z，判断是否为空
        }
    }
    if (args.size() == 4) { //如果参数数量为4
        if (args[2] == "-eq" || args[2] == "-ne" || args[2] == "-gt" || args[2] == "-ge" || args[2] == "-lt" || args[2] == "-le") {
            //如果是整数比较
            try {
                auto int1 = stoi(VariableParse(args[1])); //解析第一个整数
                auto int2 = stoi(VariableParse(args[3])); //解析第二个整数
                if (args[2] == "-eq") return int1 == int2; //如果是-eq，判断是否相等
                else if (args[2] == "-ne") return int1 != int2; //如果是-ne，判断是否不等
                else if (args[2] == "-gt") return int1 > int2; //如果是-gt，判断是否大于
                else if (args[2] == "-ge") return int1 >= int2; //如果是-ge，判断是否大于等于
                else if (args[2] == "-lt") return int1 < int2; //如果是-lt，判断是否小于
                else if (args[2] == "-le") return int1 <= int2; //如果是-le，判断是否小于等于
            }
            catch (...) { //如果转换失败
                return -1;
            }
        }
        if (args[2] == "==" || args[2] == "=" || args[2] == "!=" || args[2] == "<" || args[2] == "<=" || args[2] == ">" || args[2] == ">=") {
            //如果是字符串比较
            auto str1 = VariableParse(args[1]); //解析第一个字符串
            auto str2 = VariableParse(args[3]); //解析第二个字符串
            if (args[2] == "==") return str1 == str2; //等于
            if (args[2] == "=") return str1 == str2; //等于
            if (args[2] == "!=") return str1 != str2; //不等于
            if (args[2] == "<") return str1 < str2; //小于
            if (args[2] == "<=") return str1 <= str2; //小于等于
            if (args[2] == ">") return str1 > str2; //大于
            if (args[2] == ">=") return str1 >= str2; //大于等于
        }
    }
    return -1;
}
void Mytest(const StrVec &args, int input_fd, int output_fd, int err_fd) { //test命令
    if (args.size() > 4 || args.size() < 3) { //如果参数数量不正确
        CHARS_WRITE(err_fd, "Invalid argument of test command.\n");
        return;
    }
    int res = ExecTest(args); //执行test命令
    if (~res) { //如果执行成功
        CHARS_WRITE(output_fd, res ? "true" : "false");
    } else { //如果执行失败
        CHARS_WRITE(err_fd, "Invalid argument of test command.\n");
    }
}
void MyJobs(const StrVec &args, int input_fd, int output_fd, int err_fd) { //显示所有运行中的任务
    if (args.size() > 1) { //如果参数数量大于1
        CHARS_WRITE(err_fd, "Too many arguments for jobs command.\n");
        return; //返回
    }
    fo(i, jobs_front, jobs_back - 1)
        if (jobs[i].status != Done) { //如果任务未完成
            auto msg = (string) "[" + to_string(i) + "]\t" + kStatusStr[jobs[i].status] + "\t\"" + jobs[i].command + "\"\t" + to_string(jobs[i].pgid) + "\n";
            STR_WRITE(STDOUT_FILENO, msg); //输出任务信息
        }
}

void ExecSingleCmd(const StrVec &args, int input_fd, int output_fd, int err_fd) { //执行单个命令
    if (args[0] == "cd") { //如果是cd命令
        MyCd(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "pwd") { //如果是pwd命令
        MyPwd(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "time") { //如果是time命令
        MyTime(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "clr") { //如果是clr命令
        MyClr(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "dir") { //如果是dir命令
        MyDir(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "set") { //如果是set命令
        MySet(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "bg") { //如果是bg命令
        MyBg(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "fg") { //如果是fg命令
        MyFg(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "jobs") { //如果是jobs命令
        MyJobs(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "exit") { //如果是exit命令
        MyExit(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "echo") { //如果是echo命令
        MyEcho(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "umask") { //如果是umask命令
        MyUmask(args, input_fd, output_fd, err_fd);
    } else if (args[0] == "test") { //如果是test命令
        Mytest(args, input_fd, output_fd, err_fd);
    } else { //如果是外部命令
        MyCall(args, input_fd, output_fd, err_fd);
    }
}

void ExecRedirect(const string &cmd_str, int input_fd = STDIN_FILENO, int output_fd = STDOUT_FILENO, int err_fd = STDERR_FILENO) { //执行命令重定向
    StrVec elements = split(cmd_str); //分割命令
    StrVec args;
    elements.push_back(""); //添加一个占位符
    fo(i, 0, elements.size() - 2) {
        if (elements[i] == ">") { //输出重定向，清空
            auto fd = open(elements[i + 1].data(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
            output_fd = fd;
            ++i;
        } else if (elements[i] == ">>") { //输出重定向，追加
            auto fd = open(elements[i + 1].data(), O_WRONLY | O_APPEND | O_CREAT, 0666);
            output_fd = fd;
            ++i;
        } else if (elements[i] == "<") { //输入重定向
            auto fd = open(elements[i + 1].data(), O_RDONLY);
            input_fd = fd;
            ++i;
        } else if (elements[i] == "2>") { //错误重定向，清空
            auto fd = open(elements[i + 1].data(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
            err_fd = fd;
            ++i;
        } else if (elements[i] == "2>>") { //错误重定向，追加
            auto fd = open(elements[i + 1].data(), O_WRONLY | O_APPEND | O_CREAT, 0666);
            err_fd = fd;
            ++i;
        } else {
            args.emplace_back(elements[i]); //添加到参数列表
        }
    }
    ExecSingleCmd(args, input_fd, output_fd, err_fd); //执行单个命令
    if (input_fd != STDIN_FILENO) {
        close(input_fd); //关闭输入文件
    }
    if (output_fd != STDOUT_FILENO) {
        close(output_fd); //关闭输出文件
    }
    if (err_fd != STDERR_FILENO) {
        close(err_fd); //关闭错误输出文件
    }
}

int ExecCmdPipe(const string &cmd_str, int input_fd = STDIN_FILENO, int output_fd = STDOUT_FILENO, int err_fd = STDERR_FILENO) { //执行命令的管道部分
    StrVec cmds = split(cmd_str, "|"); //以管道符分割命令
    for (auto &o: cmds) trim(o); //去除命令前后空格
    int fds[2];
    vector<pair<int, int>> pipe_fds; //管道文件描述符集合
    pid_t pgid = -1; //进程组id
    fo(i, 0, (int) cmds.size() - 2) {
        if (pipe(fds) == -1) { //尝试创建管道，如果失败
            auto msg = (string) "Create pipe failed: " + (string) strerror(errno);
            STR_WRITE(err_fd, msg);
            return -1; //返回-1
        }
        auto pid = fork(); //创建子进程
        if (pid == -1) { //如果创建子进程失败
            auto msg = (string) "Fork failed when execute pipe: " + strerror(errno);
            STR_WRITE(err_fd, msg);
            return -1; //返回-1
        }
        if (pgid == -1) { //如果进程组id为-1，则设置进程组id为子进程id
            pgid = pid;
        }
        setpgid(pid, pgid); //设置进程组id
        if (pid == 0) { //如果是子进程
            signal(SIGINT, SIG_DFL); //恢复默认的中断信号处理函数（ctrl+c）
            signal(SIGTSTP, SIG_DFL); //恢复默认的停止信号处理函数（ctrl+z）
            if (i) { //如果不是第一个命令，从上一个管道的读取口读取，写入到下一个管道的写入口
                ExecRedirect(cmds[i], pipe_fds.back().first, fds[1]);
            } else { //如果是第一个命令，则输入为输入文件描述符
                ExecRedirect(cmds[i], input_fd, fds[1]);
            }
            exit(0); //退出子进程
        }
        pipe_fds.emplace_back(fds[0], fds[1]); //将管道添加到集合中
        cerr<<fds[0]<<" "<<fds[1]<<endl;
    }
    auto pid = fork(); //创建子进程
    if (pid == -1) { //如果创建子进程失败
        auto msg = (string) "Fork failed when execute pipe: " + strerror(errno);
        STR_WRITE(err_fd, msg);
        return -1; //返回-1
    }
    if (pgid == -1) { //如果进程组id为-1，则设置进程组id为子进程id
        pgid = pid;
    }
    setpgid(pid, pgid); //设置进程组id
    bool child_flag = true;
    auto first_cmd = split(cmds.front()).front(); //获取第一个命令
    if (cmds.size() == 1 && (first_cmd == "fg" || first_cmd == "bg" || first_cmd == "cd" || first_cmd == "exit" || first_cmd == "set")) {
        child_flag = false; //不在子进程中执行的命令
    }
    if (pid == 0) {
        signal(SIGINT, SIG_DFL); //恢复默认的中断信号处理函数（ctrl+c）
        signal(SIGTSTP, SIG_DFL); //恢复默认的停止信号处理函数（ctrl+z）
        if (cmds.size() > 1) { //如果不是最后一个命令，从上一个管道的读取口读取，写入到输出文件描述符
            ExecRedirect(cmds.back(), pipe_fds.back().first, output_fd);
        } else if (child_flag) { //如果在子进程中执行的命令
            ExecRedirect(cmds.back(), input_fd, output_fd);
        }
        exit(0);
    }
    if (!child_flag) { //如果不在子进程中执行命令
        ExecRedirect(cmds.back(), input_fd, output_fd);
    }
    for (auto &o: pipe_fds) { //关闭管道文件描述符
        close(o.first);
        close(o.second);
    }
    cerr<<"pipe closed"<<endl;
    return pgid; //返回进程组id
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

void ShowPrompt() {
//    sleep(1);
    string msg = (string) "[" + user_name + "@" + host_name + " " + work_dir + "]$ ";
    STR_WRITE(STDOUT_FILENO, msg);
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
            jobs[jobs_back]=Job(pgid,Running,cmds[i]);
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
//        jobs[front_job].status = Done;
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
//    signal(SIGCHLD,SIG_IGN);
//    signal(SIGCHLD, SignalHandler);
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
            close(cmd_input_fd);
        }
    } else {
        terminal_input = true;
        InputAndExec(STDIN_FILENO);
    }
    return 0;
}
