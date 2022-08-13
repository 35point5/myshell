#include <cstring> //std::string类所需头文件
#include <vector> //std::vector类所需头文件
#include <unistd.h> //read write等系统调用所需头文件
#include <fcntl.h> //文件操作所需头文件
#include <sys/wait.h> //wait等系统调用所需头文件
#include <pwd.h> //getpwuid等系统调用所需头文件
#include <dirent.h> //opendir等系统调用所需头文件
#include <algorithm> //std::sort排序所需头文件
#include <sys/stat.h> //stat等系统调用所需头文件
#include <map>

#define fo(i, a, b) for(int i=(a);i<=(b);++i) //简化for循环
#define STR_WRITE(fd, str) write(fd,str.data(),str.length()) //输出string到指定fd
#define CHARS_WRITE(fd, chars) write(fd,chars,strlen(chars)) //输出char*到指定fd
#define DUP(old_fd, new_fd) dup2(old_fd, new_fd); close(old_fd); //复制指定fd到新fd
using namespace std;
pid_t front_job = -1; //前台任务组id
typedef vector<string> StrVec;
const size_t kBufferSize = 8192; //缓冲区大小
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
string program_path;
string help_path;
map<string,string> help; //帮助信息
int jobs_front = 1, jobs_back = 1; //任务队列首尾
char buf[kBufferSize]; //缓冲区
StrVec Split(const string &s, const string &delimiters = " \t") { //用分割符集合中的任意一个字符分割字符串
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
string Join(const StrVec &strs, const string &delimiters){ //字符串拼接
    string res;
    for(auto &str:strs){ //遍历字符串集合
        res += delimiters + str; //拼接字符串
    }
    return res; //返回结果
}
void Trim(string &s) { //去除字符串前后的空白字符
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
void MyEcho(const StrVec &args) { //执行echo命令
    fo(i, 1, args.size() - 1) {
        STR_WRITE(STDOUT_FILENO, VariableParse(args[i])); //输出解析后参数
        if (i < args.size() - 1)
            CHARS_WRITE(STDOUT_FILENO, " "); //输出空格
    }
    CHARS_WRITE(STDOUT_FILENO, "\n"); //输出换行符
}
void MyExit(const StrVec &args) { //执行exit命令
    if (args.size() > 2) { //如果参数数量大于2
        CHARS_WRITE(STDERR_FILENO, "Too many arguments for exit command.\n");
        return; //退出
    }
    exit(0); //结束当前进程
}
void MyHelp(const StrVec &args) {
    if (args.size() > 2) { //如果参数数量大于2
        CHARS_WRITE(STDERR_FILENO, "Too many arguments for help command.\n");
        return; //退出
    }
    string entry;
    if (args.size() == 2) { //如果参数数量为2
        entry = args[1]; //获取参数
    } else {
        entry = "myshell"; //默认为myshell全局帮助
    }
    int fds[2];
    pipe(fds); //创建管道
    if (!fork()) {
        DUP(fds[0], STDIN_FILENO); //将管道的读端复制到标准输入
        close(fds[1]); //关闭管道的写端
        execlp("more", "more", nullptr); //执行more命令
        exit(0);
    } else {
        STR_WRITE(fds[1],entry);
        STR_WRITE(fds[1], help[entry]); //输出帮助
        close(fds[0]);
        close(fds[1]);
        wait(nullptr); //等待子进程结束
    }
}
void MyExec(const StrVec &args) { //执行exec命令
    char **s = new char *[args.size() + 1]; //分配参数数组
    fo(i, 0, args.size() - 1) {
        s[i] = new char[args[i].length() + 1];
        strcpy(s[i], args[i].data()); //复制参数到字符数组
    }
    s[args.size()] = nullptr; //最后一个参数为空
    execvp(s[0], s); //执行命令
    auto msg = (string) "Execute " + args[0] + " failed: " + strerror(errno) + "\n";
    STR_WRITE(STDERR_FILENO, msg); //执行到此处说明执行失败
    for (int i = 0; i < args.size(); ++i) {
        delete[] s[i]; //释放参数数组
    }
    delete[] s; //释放参数数组
}
void MyCall(const StrVec &args) { //执行外部命令
    if (!fork()) { //新建子进程执行外部命令
        setenv("parent",program_path.data(),1); //设置parent环境变量
        MyExec(args);
        exit(0);
    } else{
        wait(nullptr); //等待子进程结束
    }
}

void MyFg(const StrVec &args) { //把后台任务调到前台
    if (args.size() != 2) { //如果参数数量不为2
        CHARS_WRITE(STDERR_FILENO, "Invalid argument of fg command.\n");
        return; //退出
    }
    int job_id; //任务id
    try {
        job_id = stoi(args[1]); //将参数转换为整数
    }
    catch (...) { //如果转换失败
        CHARS_WRITE(STDERR_FILENO, "Invalid job id of fg command.\n");
        return; //退出
    }
    if (job_id < jobs_front || job_id >= jobs_back) { //如果任务id超出范围
        CHARS_WRITE(STDERR_FILENO, "Invalid job id.\n");
        return; //退出
    }
    if (jobs[job_id].status == Done) { //如果任务已结束
        CHARS_WRITE(STDERR_FILENO, "Job has terminated.\n");
        return; //退出
    }
    front_job = job_id; //设置前台任务为当前任务
    if (terminal_input) { //如果是终端输入
        STR_WRITE(STDOUT_FILENO, jobs[job_id].command); //输出任务命令
        CHARS_WRITE(STDOUT_FILENO, "\n");
    }
    ExecFront(); //执行前台任务
}

void MyBg(const StrVec &args) { //把前台任务调到后台
    if (args.size() != 2) { //如果参数数量不为2
        CHARS_WRITE(STDERR_FILENO, "Invalid argument of bg command.\n");
        return; //退出
    }
    int job_id; //任务id
    try {
        job_id = stoi(args[1]); //将参数转换为整数
    }
    catch (...) { //如果转换失败
        CHARS_WRITE(STDERR_FILENO, "Invalid job id of bg command.\n");
        return; //退出
    }
    if (job_id < jobs_front || job_id >= jobs_back) { //如果任务id超出范围
        CHARS_WRITE(STDERR_FILENO, "Invalid job id.\n");
        return; //退出
    }
    if (jobs[job_id].status == Done) { //如果任务已结束
        CHARS_WRITE(STDERR_FILENO, "Job has terminated.\n");
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
void MyCd(const StrVec &args) { //改变当前工作目录
    if (args.size() != 2) { //如果参数数量不为2
        CHARS_WRITE(STDERR_FILENO, "Invalid argument of cd command.\n");
        return; //退出
    }
    string path = args[1] == "~" ? home : args[1]; //判断是否为~
    if (chdir(path.data()) == -1) { //尝试改变工作目录，如果失败
        auto msg = (string) "Change directory failed: " + strerror(errno); //输出错误信息
        STR_WRITE(STDERR_FILENO, msg);
        return; //退出
    }
    getcwd(buf, kBufferSize); //获取当前工作目录
    path = buf;
    work_dir = Split(path, "/").back(); //设置当前工作目录
    setenv("PWD", args[1].data(), 1); //设置环境变量PWD
}
void MyPwd(const StrVec &args) { //输出当前工作目录
    if (args.size() > 1) { //如果参数数量大于1
        CHARS_WRITE(STDERR_FILENO, "Too many arguments for pwd command.\n");
        return; //退出
    }
    getcwd(buf, kBufferSize); //获取当前工作目录
    CHARS_WRITE(STDOUT_FILENO, buf); //输出当前工作目录
}
void MyTime(const StrVec &args) { //输出当前时间
    if (args.size() > 1) { //如果参数数量大于1
        CHARS_WRITE(STDERR_FILENO, "Too many arguments for time command.\n");
        return; //退出
    }
    time_t raw_time;
    time(&raw_time); //获取当前时间
    tm *tmp = localtime(&raw_time);
    strftime(buf, kBufferSize, "%Y-%m-%d %H:%M:%S", tmp); //格式化为字符串
    CHARS_WRITE(STDOUT_FILENO, buf); //输出时间
}
void MyClr(const StrVec &args) { //清屏
    if (args.size() > 1) { //如果参数数量大于1
        CHARS_WRITE(STDERR_FILENO, "Too many arguments for clr command.\n");
        return; //退出
    }
    printf("\033c"); //清屏
}
void MyDir(const StrVec &args) { //输出当前目录下的文件
    if (args.size() > 2) { //如果参数数量不为2
        CHARS_WRITE(STDERR_FILENO, "Invalid argument of dir command.\n");
        return; //退出
    }
    string path;
    if (args.size() == 1) { //如果参数数量为1
        path = "."; //设置为当前目录
    }
    else { //如果参数数量为2
        path = args[1] == "~" ? home : args[1]; //判断是否为~
    }
    DIR *dir;
    if ((dir = opendir(path.data())) == nullptr) { //尝试打开目录，如果失败
        auto msg = (string) "Open directory failed: " + strerror(errno);
        STR_WRITE(STDERR_FILENO, msg);
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
        STR_WRITE(STDOUT_FILENO, file); //输出文件名
        CHARS_WRITE(STDOUT_FILENO, "\n");
    }
    closedir(dir); //关闭目录
}
void MySet(const StrVec &args) { //设置环境变量
    if (args.size() > 3) { //如果参数数量大于3
        CHARS_WRITE(STDERR_FILENO, "Too many arguments for set command.\n");
        return; //退出
    }
    if (args.size() == 1) { //如果参数数量为1，输出所有环境变量
        for (int i = 0; environ[i]; ++i) { //循环输出环境变量
            CHARS_WRITE(STDOUT_FILENO, environ[i]);
            CHARS_WRITE(STDOUT_FILENO, "\n");
        }
    } else if (args.size() == 2) { //如果参数数量为2，输出指定环境变量
        CHARS_WRITE(STDOUT_FILENO, getenv(args[1].data())); //输出环境变量
        CHARS_WRITE(STDOUT_FILENO, "\n");
    } else { //如果参数数量为3，设置环境变量
        if (setenv(args[1].data(), args[2].data(), 1) == -1) { //尝试设置环境变量，如果失败
            auto msg = (string) "Set environment variable failed: " + strerror(errno);
            STR_WRITE(STDERR_FILENO, msg);
        }
    }
}
void MyUmask(const StrVec &args) { //设置umask
    if (args.size() > 2) { //如果参数数量大于2
        CHARS_WRITE(STDERR_FILENO, "Too many arguments for umask command.\n");
        return;
    }
    if (args.size() == 1) { //如果参数数量为1，输出当前umask
        mode_t old_mask = umask(0); //获取当前umask
        umask(old_mask); //恢复umask
        STR_WRITE(STDOUT_FILENO, to_string(old_mask)); //输出umask
        CHARS_WRITE(STDOUT_FILENO, "\n");
    } else { //如果参数数量为2，设置umask
        try {
            mode_t mask = stoi(args[1]); //尝试转换为整数
            umask(mask); //设置umask
            auto msg = (string) "New mask :" + to_string(mask) + "\n"; //输出新umask
            STR_WRITE(STDOUT_FILENO, msg);
        }
        catch (...) { //如果转换失败
            CHARS_WRITE(STDERR_FILENO, "Mask should be an valid integer.\n");
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
void Mytest(const StrVec &args) { //test命令
    if (args.size() > 4 || args.size() < 3) { //如果参数数量不正确
        CHARS_WRITE(STDERR_FILENO, "Invalid argument of test command.\n");
        return;
    }
    int res = ExecTest(args); //执行test命令
    if (~res) { //如果执行成功
        CHARS_WRITE(STDOUT_FILENO, res ? "true\n" : "false\n");
    } else { //如果执行失败
        CHARS_WRITE(STDERR_FILENO, "Invalid argument of test command.\n");
    }
}
void MyJobs(const StrVec &args) { //显示所有运行中的任务
    if (args.size() > 1) { //如果参数数量大于1
        CHARS_WRITE(STDERR_FILENO, "Too many arguments for jobs command.\n");
        return; //返回
    }
    fo(i, jobs_front, jobs_back - 1)
        if (jobs[i].status != Done) { //如果任务未完成
            auto msg = (string) "[" + to_string(i) + "]\t" + kStatusStr[jobs[i].status] + "\t\"" + jobs[i].command + "\"\t" + to_string(jobs[i].pgid) + "\n";
            STR_WRITE(STDOUT_FILENO, msg); //输出任务信息
        }
}

void ExecSingleCmd(StrVec args) { //执行单个命令
    if (args[0] == "cd") { //如果是cd命令
        MyCd(args);
    } else if (args[0] == "pwd") { //如果是pwd命令
        MyPwd(args);
    } else if (args[0] == "time") { //如果是time命令
        MyTime(args);
    } else if (args[0] == "clr") { //如果是clr命令
        MyClr(args);
    } else if (args[0] == "dir") { //如果是dir命令
        MyDir(args);
    } else if (args[0] == "set") { //如果是set命令
        MySet(args);
    } else if (args[0] == "bg") { //如果是bg命令
        MyBg(args);
    } else if (args[0] == "fg") { //如果是fg命令
        MyFg(args);
    } else if (args[0] == "jobs") { //如果是jobs命令
        MyJobs(args);
    } else if (args[0] == "exit") { //如果是exit命令
        MyExit(args);
    } else if (args[0] == "echo") { //如果是echo命令
        MyEcho(args);
    } else if (args[0] == "help") { //如果是help命令
        MyHelp(args);
    } else if (args[0] == "umask") { //如果是umask命令
        MyUmask(args);
    } else if (args[0] == "exec") { //如果是exec命令
        args.erase(args.begin()); //去除开头的exec
        MyExec(args);
    } else if (args[0] == "test") { //如果是test命令
        Mytest(args);
    } else { //如果是外部命令
        MyCall(args);
    }
}

void ExecRedirect(const string &cmd_str, int input_fd = STDIN_FILENO, int output_fd = STDOUT_FILENO, int err_fd = STDERR_FILENO) { //执行命令重定向
    StrVec elements = Split(cmd_str); //分割命令
    StrVec args;
    elements.push_back(""); //添加一个占位符
    fo(i, 0, elements.size() - 2) {
        if (elements[i] == ">") { //输出重定向，清空
            auto fd = open(elements[i + 1].data(), O_WRONLY | O_CREAT | O_TRUNC, 0666); //打开文件
            DUP(fd, STDOUT_FILENO); //复制到标准输出
            ++i;
        } else if (elements[i] == ">>") { //输出重定向，追加
            auto fd = open(elements[i + 1].data(), O_WRONLY | O_APPEND | O_CREAT, 0666);
            DUP(fd, STDOUT_FILENO);
            ++i;
        } else if (elements[i] == "<") { //输入重定向
            auto fd = open(elements[i + 1].data(), O_RDONLY);
            DUP(fd, STDIN_FILENO);
            ++i;
        } else if (elements[i] == "2>") { //错误重定向，清空
            auto fd = open(elements[i + 1].data(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
            DUP(fd, STDERR_FILENO);
            ++i;
        } else if (elements[i] == "2>>") { //错误重定向，追加
            auto fd = open(elements[i + 1].data(), O_WRONLY | O_APPEND | O_CREAT, 0666);
            DUP(fd, STDERR_FILENO);
            ++i;
        } else {
            args.emplace_back(elements[i]); //添加到参数列表
        }
    }
    ExecSingleCmd(args); //执行单个命令
}

int ExecCmdPipe(const string &cmd_str, int input_fd = STDIN_FILENO, int output_fd = STDOUT_FILENO, int err_fd = STDERR_FILENO) { //执行命令的管道部分
    StrVec cmds = Split(cmd_str, "|"); //以管道符分割命令
    for (auto &o: cmds) Trim(o); //去除命令前后空格
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
                DUP(pipe_fds.back().first, STDIN_FILENO) //复制到标准输入
                close(pipe_fds.back().second); //关闭上一个管道的写入口
                DUP(fds[1],STDOUT_FILENO) //复制到标准输出
                close(fds[0]); //关闭当前管道的读取口
                ExecRedirect(cmds[i]);
            } else { //如果是第一个命令，则输入为输入文件描述符
                DUP(fds[1], STDOUT_FILENO)
                close(fds[0]); //关闭读取口
                ExecRedirect(cmds[i]);
            }
            exit(0); //退出子进程
        }
        pipe_fds.emplace_back(fds[0], fds[1]); //将管道添加到集合中
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
    auto first_cmd = Split(cmds.front()).front(); //获取第一个命令
    if (cmds.size() == 1 && (first_cmd == "fg" || first_cmd == "bg" || first_cmd == "cd" || first_cmd == "exit" || first_cmd == "set" || first_cmd == "help")) {
        child_flag = false; //不在子进程中执行的命令
    }
    if (pid == 0) {
        signal(SIGINT, SIG_DFL); //恢复默认的中断信号处理函数（ctrl+c）
        signal(SIGTSTP, SIG_DFL); //恢复默认的停止信号处理函数（ctrl+z）
        if (cmds.size() > 1) { //如果不止一个命令，从上一个管道的读取口读取，写入到输出文件描述符
            DUP(pipe_fds.back().first, STDIN_FILENO)
            close(pipe_fds.back().second); //关闭上一个管道的写入口
            ExecRedirect(cmds.back());
        } else if (child_flag) { //如果在子进程中执行的命令
            ExecRedirect(cmds.back());
        }
        exit(0);
    }
    if (!child_flag) { //如果不在子进程中执行命令
        ExecRedirect(cmds.back());
    }
    for (auto &o: pipe_fds) { //关闭管道文件描述符
        close(o.first);
        close(o.second);
    }
    return pgid; //返回进程组id
}
void CheckJobs() { //检查并输出队列中刚刚结束的任务
    fo(i, jobs_front, jobs_back - 1)
        if (jobs[i].status == Running) { //如果正在运行
            pid_t pid;
            while ((pid = waitpid(-jobs[i].pgid, nullptr, WNOHANG)) > 0); //检查任务是否结束
            if (pid == -1 && errno == ECHILD) { //如果子进程已经结束
                jobs[i].status = Done; //修改状态为已结束
                if (terminal_input) { //如果是在终端中执行的命令
                    auto msg = (string) "[" + to_string(i) + "]\tDone\t\"" + jobs[i].command + "\"\t" + to_string(jobs[i].pgid) + "\n";
                    STR_WRITE(STDOUT_FILENO, msg); //输出任务信息
                }
            }
        }
    while (jobs_front < jobs_back && jobs[jobs_back - 1].status == Done) { //如果队列末有已结束的任务，则删除
        --jobs_back;
    }
    if (jobs_front == jobs_back) { //如果队列为空，则清空队列
        jobs_front = jobs_back = 1;
    }
}
void ShowPrompt() { //输出提示符
    string msg = (string) "[" + user_name + "@" + host_name + " " + work_dir + "]$ ";
    STR_WRITE(STDOUT_FILENO, msg);
}
void ExecCmdBackground(const string &cmd_str, int input_fd = STDIN_FILENO, int output_fd = STDOUT_FILENO, int err_fd = STDERR_FILENO) { //处理命令的后台执行部分
    StrVec cmds = Split(cmd_str, "&"); //以&分割命令
    fo(i, 0, (int) cmds.size() - 1) {
        Trim(cmds[i]); //去除命令前后的空格
        if (jobs_back == kMaxJobs) { //如果队列已满，则提示错误
            string msg = "Too many jobs";
            STR_WRITE(err_fd, msg);
        } else {
            auto pgid = ExecCmdPipe(cmds[i], input_fd, output_fd, err_fd); //执行下一部命令（管道）
            jobs[jobs_back++]=Job(pgid,Running,cmds[i]); //添加任务到队列中
            if (i == cmds.size() - 1 && Split(cmd_str).back() != "&") { //如果是最后一个命令且在前台执行
                front_job = jobs_back - 1; //设置前台任务为最后一个任务
                ExecFront(); //执行前台任务
            } else {
                if (terminal_input) {
                    auto msg = (string) "[" + to_string(jobs_back - 1) + "]\tRunning\t\"" + cmds[i] + "\"\t" + to_string(pgid) + "\n";
                    STR_WRITE(STDOUT_FILENO, msg); //输出任务信息
                }
            }
        }
    }
}
void InputAndExec(int cmd_input_fd, int input_fd = STDIN_FILENO, int output_fd = STDOUT_FILENO, int err_fd = STDERR_FILENO) { //输入并执行命令
    char cmd_buf[kBufferSize]; //命令缓冲区
    ssize_t cmd_len; //命令长度
    string cmd_str; //命令字符串
    if (terminal_input) {
        ShowPrompt(); //输出提示符
    }
    while ((cmd_len = read(cmd_input_fd, cmd_buf, kBufferSize)) > 0) { //读取命令到缓冲区
        fo(i, 0, cmd_len - 1) //枚举缓冲区中的每个字符
            if (cmd_buf[i] == '\n') { //如果是换行符
                CheckJobs(); //检查并输出队列中刚刚结束的任务
                if (!cmd_str.empty()) ExecCmdBackground(cmd_str); //如果命令不为空，则执行命令
                cmd_str.clear(); //清空命令
                if (terminal_input) {
                    ShowPrompt(); //输出提示符
                }
            } else
                cmd_str += cmd_buf[i]; //添加字符到命令字符串末尾
    }
    if (cmd_len < 0) { //如果读取失败
        auto msg = (string) "Read command error: " + strerror(errno);
        STR_WRITE(err_fd, msg); //输出错误信息
    }
}
void SignalHandler(int signal) { //信号处理函数
    if (signal == SIGINT) { //如果是SIGINT信号（ctrl c）
        CHARS_WRITE(STDOUT_FILENO, "\n");
        if (front_job == -1) return;
        kill(-jobs[front_job].pgid, SIGINT); //发送SIGINT信号给前台任务的进程组
    } else if (signal == SIGTSTP) { //如果是SIGTSTP信号（ctrl z）
        CHARS_WRITE(STDOUT_FILENO, "\n");
        if (front_job == -1) return;
        kill(jobs[front_job].pgid, SIGTSTP); //发送SIGTSTP信号给前台任务的进程组
        if (terminal_input) {
            auto msg = (string) "[" + to_string(front_job) + "]\tStopped\t\"" + jobs[front_job].command + "\"\t" + to_string(jobs[front_job].pgid) + "\n";
            STR_WRITE(STDOUT_FILENO, msg); //输出任务信息
        }
        jobs[front_job].status = Stopped; //更改任务状态为已停止
        front_job = -1; //清除前台任务
    }
}
void GetHelp(){
    auto myshell_dir = Split(program_path,"/");
    myshell_dir.pop_back();
    myshell_dir.push_back("help");
    help_path = Join(myshell_dir, "/");
    auto help_fd = open(help_path.c_str(), O_RDONLY);
    if (help_fd < 0) {
        auto msg = (string) "Open help file error: " + strerror(errno);
        STR_WRITE(STDERR_FILENO, msg);
        return;
    }
    read(help_fd, buf, kBufferSize);
    string help_str = buf;
    auto entrys = Split(help_str, "#");
    for (auto &entry :entrys){
        auto help_content = Split(entry, "~");
        Trim(help_content.front());
        help[help_content.front()] = help_content.back();
    }
}
void Init() { //初始化
    gethostname(buf, kBufferSize); //获取主机名
    host_name = buf; //设置主机名
    getlogin_r(buf, kBufferSize); //获取登录用户名
    user_name = buf; //设置登录用户名
    getcwd(buf, kBufferSize); //获取当前工作目录
    work_dir = buf;
    work_dir = Split(work_dir, "/").back(); //设置工作目录
    home = getpwuid(getuid())->pw_dir; //设置主目录
    if (readlink("/proc/self/exe", buf, kBufferSize) == -1) { //尝试获取当前程序路径，如果失败
        CHARS_WRITE(STDOUT_FILENO, "Readlink error: ");
        CHARS_WRITE(STDOUT_FILENO, strerror(errno));
        CHARS_WRITE(STDOUT_FILENO, "\n"); //输出错误信息
    }
    program_path=buf; //设置当前程序路径
    if (setenv("shell", buf, 1) == -1) { //尝试设置shell环境变量，如果失败
        CHARS_WRITE(STDOUT_FILENO, "Setenv shell error: ");
        CHARS_WRITE(STDOUT_FILENO, strerror(errno));
        CHARS_WRITE(STDOUT_FILENO, "\n"); //输出错误信息
    }
    GetHelp();
    signal(SIGINT, SignalHandler); //注册SIGINT信号处理函数
    signal(SIGTSTP, SignalHandler); //注册SIGTSTP信号处理函数
}
int main(int argc, char **argv) { //主函数
    Init(); //初始化
    if (argc > 1) { //如果指定了输入文件
        terminal_input = false;
        fo(i, 1, argc - 1) { //依次执行每个文件中的命令
            auto cmd_input_fd = open(argv[i], O_RDONLY); //打开文件
            if (cmd_input_fd == -1) { //如果打开失败
                auto msg = (string) "Open file " + argv[i] + " failed: " + strerror(errno) + "\n";
                STR_WRITE(STDERR_FILENO, msg); //输出错误信息
            }
            InputAndExec(cmd_input_fd); //输入并执行命令
            close(cmd_input_fd); //关闭文件
        }
    } else {
        terminal_input = true; //设置为从终端输入
        InputAndExec(STDIN_FILENO); //输入并执行命令
    }
    return 0; //返回0
}
