#include"../Header/shaderClass.h"
std::string get_file_contents(const char* filename)
{
	// Пытаемся открыть файл
	std::ifstream in(filename, std::ios::binary);

	if (!in) {
		// Если не открылся, выводим ПОЛНЫЙ путь, по которому программа его искала
		char fullPath[MAX_PATH];
		GetFullPathNameA(filename, MAX_PATH, fullPath, NULL);

		std::cerr << "--- ОШИБКА ЗАГРУЗКИ ---" << std::endl;
		std::cerr << "Файл не найден: " << filename << std::endl;
		std::cerr << "Я искал его здесь: " << fullPath << std::endl;
		std::cerr << "-----------------------" << std::endl;

		throw(2); // Твой errno 2
	}

	std::string contents;
	in.seekg(0, std::ios::end);
	contents.resize(in.tellg());
	in.seekg(0, std::ios::beg);
	in.read(&contents[0], contents.size());
	in.close();
	return(contents);
}
Shader::Shader(const char* computeFile)
{
		std::string computeCode = get_file_contents(computeFile);
	const char* computeSource = computeCode.c_str();
		GLuint computeShader = glCreateShader(GL_COMPUTE_SHADER);
		glShaderSource(computeShader, 1, &computeSource, NULL);
	glCompileShader(computeShader);
		compileErrors(computeShader, "COMPUTE");
		ID = glCreateProgram();
	glAttachShader(ID, computeShader);
	glLinkProgram(ID);
		compileErrors(ID, "PROGRAM");
		glDeleteShader(computeShader);
}
Shader::Shader(const char* vertexFile, const char* fragmentFile)
{
		std::string vertexCode = get_file_contents(vertexFile);
	std::string fragmentCode = get_file_contents(fragmentFile);
		const char* vertexSource = vertexCode.c_str();
	const char* fragmentSource = fragmentCode.c_str();
		GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vertexShader, 1, &vertexSource, NULL);
		glCompileShader(vertexShader);
		compileErrors(vertexShader, "VERTEX");
		GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
		glCompileShader(fragmentShader);
		compileErrors(fragmentShader, "FRAGMENT");
		ID = glCreateProgram();
		glAttachShader(ID, vertexShader);
	glAttachShader(ID, fragmentShader);
		glLinkProgram(ID);
		compileErrors(ID, "PROGRAM");
		glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
}
Shader::Shader(const char* vertexFile, const char* fragmentFile, const char* geometryFile)
{
		std::string vertexCode = get_file_contents(vertexFile);
	std::string fragmentCode = get_file_contents(fragmentFile);
	std::string geometryCode = get_file_contents(geometryFile);
		const char* vertexSource = vertexCode.c_str();
	const char* fragmentSource = fragmentCode.c_str();
	const char* geometrySource = geometryCode.c_str();
		GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vertexShader, 1, &vertexSource, NULL);
		glCompileShader(vertexShader);
		compileErrors(vertexShader, "VERTEX");
		GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
		glCompileShader(fragmentShader);
		compileErrors(fragmentShader, "FRAGMENT");
		GLuint geometryShader = glCreateShader(GL_GEOMETRY_SHADER);
		glShaderSource(geometryShader, 1, &geometrySource, NULL);
		glCompileShader(geometryShader);
		compileErrors(geometryShader, "GEOMETRY");
		ID = glCreateProgram();
		glAttachShader(ID, vertexShader);
	glAttachShader(ID, fragmentShader);
	glAttachShader(ID, geometryShader);
		glLinkProgram(ID);
		compileErrors(ID, "PROGRAM");
		glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
	glDeleteShader(geometryShader);
}
void Shader::Activate()
{
	glUseProgram(ID);
}
void Shader::Delete()
{
	glDeleteProgram(ID);
}
void Shader::compileErrors(unsigned int shader, const char* type)
{
		GLint hasCompiled;
		char infoLog[1024];
	if (type != "PROGRAM")
	{
		glGetShaderiv(shader, GL_COMPILE_STATUS, &hasCompiled);
		if (hasCompiled == GL_FALSE)
		{
			glGetShaderInfoLog(shader, 1024, NULL, infoLog);
			std::cout << "SHADER_COMPILATION_ERROR for:" << type << "\n" << infoLog << std::endl;
		}
	}
	else
	{
		glGetProgramiv(shader, GL_LINK_STATUS, &hasCompiled);
		if (hasCompiled == GL_FALSE)
		{
			glGetProgramInfoLog(shader, 1024, NULL, infoLog);
			std::cout << "SHADER_LINKING_ERROR for:" << type << "\n" << infoLog << std::endl;
		}
	}
}