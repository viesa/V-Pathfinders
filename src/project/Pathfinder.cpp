#include "Pathfinder.h"

Pathfinder::Pathfinder()
    : m_state(State::WaitingForStart),
      m_activeNodeUID(-1),
      m_sleepDelay(sf::seconds(0.01f)),
      m_minorDelayTimer(0),
      m_minorDelay(false)
{
}

Pathfinder::~Pathfinder()
{
    CollectFinder();
}

void Pathfinder::DrawAnticipation()
{
    if (m_activeNodeUID != -1 && m_state == State::Finding || m_state == State::Paused || m_state == State::Finished)
    {
        for (Node *node = &GetNode(m_activeNodeUID); node->GetViaUID() != -1; node = &GetNode(node->GetViaUID()))
        {
            Camera::DrawLine(node->GetPosition(), GetNode(node->GetViaUID()).GetPosition(), sf::Color::Red);
        }
        for (auto &node : m_finalPath)
        {
            Camera::DrawPoint(node->GetPosition(), sf::Color::Magenta, 3.0f);
        }
        Camera::DrawPoint(GetNodes().at(m_activeNodeUID).GetPosition(), sf::Color::Red);
    }
}

void Pathfinder::DrawViaConnections()
{
    for (auto &[uid, node] : m_nodes)
    {
        if (node.GetViaUID() != -1)
            Camera::DrawLine(node.GetPosition(), GetNode(node.GetViaUID()).GetPosition(), sf::Color(150, 150, 150, 20));
    }
}

void Pathfinder::DrawNeighbors()
{
    for (auto &[uid, node] : m_nodes)
    {
        for (auto &neighborUID : node.GetNeighbors())
        {
            Camera::DrawLine(node.GetPosition(), GetNode(neighborUID).GetPosition(), sf::Color(255, 0, 255, 20));
        }
    }
}

void Pathfinder::DrawResult()
{
    if (m_pathWasFound)
    {
        if (m_nFinalPathNodes < m_finalPath.size() - 1)
        {
            m_finalPathTimer += Clock::Delta();
            if (m_finalPathTimer.asSeconds() > 0.05f)
            {
                m_nFinalPathNodes++;
                m_finalPathTimer = sf::Time::Zero;
            }
        }
        for (size_t i = 0; i < m_nFinalPathNodes; i++)
        {
            Camera::DrawPoint(m_finalPath[i]->GetPosition(), sf::Color(0, 150, 0), 3.0f);
        }
        Camera::DrawPoint(GetNode(m_traverseGrid->GetStartUID()).GetPosition(), sf::Color(0, 150, 0));
        Camera::DrawPoint(m_finalPath[m_nFinalPathNodes]->GetPosition(), sf::Color::Green, 5.0f);
    }
    else
    {
        Camera::DrawPoint(GetNode(m_traverseGrid->GetStartUID()).GetPosition(), sf::Color::Red, 10.0f);
        Camera::DrawPoint(GetNode(m_traverseGrid->GetGoalUID()).GetPosition(), sf::Color::Red, 10.0f);
    }
}

void Pathfinder::AssignNodes(const std::map<long, Node> &nodes) noexcept
{
    m_nodes = nodes;
}

void Pathfinder::Start(long startUID, long goalUID, const std::vector<long> &subGoalsUIDs)
{
    if (m_state == State::Finished)
    {
        Restart();
    }
    if (m_state == State::WaitingForStart)
    {
        CollectFinder();
        m_state = State::Finding;
        m_finder = std::thread(&Pathfinder::FindPathThreadFn, this, startUID, goalUID, subGoalsUIDs);
    }
}

void Pathfinder::Pause()
{
    if (m_state == State::Finding)
        m_state = State::Paused;
}

void Pathfinder::Resume()
{
    if (m_state == State::Paused)
        m_state = State::Finding;
}

void Pathfinder::Restart()
{
    if (m_state == State::Finding || m_state == State::Paused || m_state == State::Finished)
    {
        CollectFinder();
        m_state = State::WaitingForStart;
        for (auto &[uid, node] : m_nodes)
        {
            node.ResetPath();
            node.ClearVisitedNeighbors();
        }
    }
}

void Pathfinder::Reset()
{
    if (m_state != State::WaitingForStart)
    {
        CollectFinder();
        m_state = State::WaitingForStart;
        for (auto &[uid, node] : m_nodes)
        {
            node.ResetPath();
            node.ClearVisitedNeighbors();
        }
    }
}

void Pathfinder::SetSleepDelay(sf::Time delay)
{
    m_sleepDelay = delay;
    m_minorDelay = (m_sleepDelay.asMicroseconds() < 1000);
}

void Pathfinder::PauseCheck()
{
    while (m_state == State::Paused && m_state != State::BeingCollected)
        sf::sleep(sf::seconds(0.01f));
}

void Pathfinder::SleepDelay()
{
    if (!m_minorDelay)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(m_sleepDelay.asMicroseconds()));
    }
    else
    {
        m_minorDelayTimer += m_sleepDelay.asMicroseconds();
        while (m_minorDelayTimer > 1000)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(1000));
            m_minorDelayTimer -= 1000;
        }
    }
}

void Pathfinder::FindPathThreadFn(long startUID, long goalUID, const std::vector<long> &subGoalsUIDs)
{
    m_finalPath.clear();

    long fromUID = startUID;
    long toUID;
    for (int i = 0; i < subGoalsUIDs.size(); i++)
    {
        toUID = subGoalsUIDs[i];
        FindPath(fromUID, toUID);
        bool result = CheckFindPathResult(fromUID, toUID);
        if (!result)
        {
            m_pathWasFound = false;
            m_state = State::Finished;
            return;
        }
        fromUID = subGoalsUIDs[i];
    };
    toUID = goalUID;
    FindPath(fromUID, toUID);
    m_pathWasFound = CheckFindPathResult(fromUID, toUID);
    m_state = State::Finished;

    m_nFinalPathNodes = 0;
    m_finalPathTimer = sf::Time::Zero;
}

bool Pathfinder::CheckFindPathResult(long fromUID, long toUID)
{
    if (m_state == State::BeingCollected)
        return false;

    bool foundPath = (GetNodes().at(toUID).WasVisited());
    if (foundPath)
    {
        AppendFinalPath(fromUID, toUID);
        for (auto &[uid, node] : m_nodes)
        {
            node.ResetPath();
        }
        return true;
    }
    else
    {
        m_pathWasFound = false;
        return false;
    }
}

void Pathfinder::AppendFinalPath(long startUID, long goalUID)
{
    std::vector<const Node *> tmp;
    for (const Node *node = &GetNode(goalUID); node != &GetNode(startUID); node = &GetNode(node->GetViaUID()))
    {
        tmp.push_back(node);
    }
    std::reverse_copy(tmp.begin(), tmp.end(), std::back_inserter(m_finalPath));
}

void Pathfinder::CollectFinder()
{
    auto savedState = m_state;
    m_state = State::BeingCollected;
    if (m_finder.joinable())
        m_finder.join();
    m_state = savedState;
}
