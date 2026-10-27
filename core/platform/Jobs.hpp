#pragma once

namespace burnhope {

// Минимальный fork-join job system. Не очередь задач на кадр вперёд — один вызов
// jobsRunAndWait запускает N независимых работ параллельно и блокирует вызывающий
// поток, пока все не закончатся. Поток вызова тоже тянет работу из того же массива
// (work-stealing через atomic-счётчик), так что при N <= 1 потоке в пуле всё равно
// не простаивает. 
//
// Используется в core/host/Host.cpp для параллельной записи кадра разных окон и
// может использоваться для CPU-тяжёлых задач (BC7/BC6H encode по блокам, etc.) —
// не привязан к рендеру.
//
// Пул принадлежит владельцу: jobsCreate / jobsDestroy. Рендер держит его на Device (deviceJobs).
struct JobPool;

[[nodiscard]] JobPool* jobsCreate();
void jobsDestroy(JobPool* pool);

using JobFn = void (*)(void* user);

struct Job {
    JobFn fn = nullptr;
    void* user = nullptr;
};

// Блокирует вызывающий поток до завершения всех jobs[0..count). count может быть 0 или 1
// (тогда работа идёт прямо на вызывающем потоке, без пула).
void jobsRunAndWait(JobPool& pool, Job* jobs, int count);

// Ставит одну работу в очередь пула и сразу возвращается; работа идёт на рабочем потоке параллельно с кадром.
// Без рабочих потоков (одно ядро) работа выполняется прямо здесь. false — очередь (kJobQueueCap) заполнена, работа не принята.
// Поток вызова не должен ждать результат под замком: результат передаётся атомарной публикацией владельца работы.
inline constexpr int kJobQueueCap = 64;
[[nodiscard]] bool jobsRun(JobPool& pool, Job job);
// Блокирует до завершения всех принятых jobsRun. Владелец данных работы вызывает её перед их уничтожением.
void jobsDrain(JobPool& pool);

// Сколько потоков реально тянут работу, включая вызывающий (для логов/диагностики).
int jobsWorkerCount(const JobPool& pool);

} // namespace burnhope
