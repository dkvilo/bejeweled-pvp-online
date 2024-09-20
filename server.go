/**
 * $Author David Kviloria
 * $Last Modified 2019
 */
package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math/rand"
	"net"
	"sync"
	"time"
)

const (
	PORT         = 8080
	BUFLEN       = 512
	BOARD_SIZE   = 8
	MIN_MATCH    = 3
	GAME_TIMEOUT = 30 * time.Second
	MAX_GAMES    = 100
)

type Tile int32

const (
	Empty Tile = iota
	Red
	Blue
	Green
	Yellow
	Purple
	Special
)

type GameState struct {
	GameID       int32
	Board        [BOARD_SIZE][BOARD_SIZE]Tile
	CurrentTurn  int32
	Player1Score int32
	Player2Score int32
	GameStarted  bool
	GameOver     bool
	Player1Addr  *net.UDPAddr
	Player2Addr  *net.UDPAddr
	LastActivity [2]time.Time
}

type PlayerMove struct {
	PlayerID int32
	FromX    int
	FromY    int
	ToX      int
	ToY      int
}

var (
	games      [MAX_GAMES]GameState
	gameCount  int
	nextGameID int = 1
	gameMutex  sync.Mutex
)

func (g *GameState) Serialize() ([]byte, error) {
	buf := new(bytes.Buffer)

	// fixed-size fields
	if err := binary.Write(buf, binary.LittleEndian, g.GameID); err != nil {
		return nil, err
	}

	if err := binary.Write(buf, binary.LittleEndian, g.Board); err != nil {
		return nil, err
	}

	if err := binary.Write(buf, binary.LittleEndian, g.CurrentTurn); err != nil {
		return nil, err
	}

	if err := binary.Write(buf, binary.LittleEndian, g.Player1Score); err != nil {
		return nil, err
	}

	if err := binary.Write(buf, binary.LittleEndian, g.Player2Score); err != nil {
		return nil, err
	}

	if err := binary.Write(buf, binary.LittleEndian, g.GameStarted); err != nil {
		return nil, err
	}

	if err := binary.Write(buf, binary.LittleEndian, g.GameOver); err != nil {
		return nil, err
	}

	return buf.Bytes(), nil
}

func joinGame(conn *net.UDPConn, addr *net.UDPAddr) {
	gameMutex.Lock()
	defer gameMutex.Unlock()

	fmt.Printf("Attempting to join game for player at %v\n", addr)

	var game *GameState
	for i := range games {
		if games[i].GameID != 0 && !games[i].GameStarted {
			game = &games[i]
			fmt.Printf("Found existing game %d\n", game.GameID)
			break
		}
	}

	if game == nil {
		for i := range games {
			if games[i].GameID == 0 {
				games[i].GameID = int32(nextGameID)
				nextGameID++
				game = &games[i]
				gameCount++
				fmt.Printf("Created new game %d\n", game.GameID)
				break
			}
		}
	}

	if game == nil {
		fmt.Println("Max games reached!")
		return
	}

	if game.Player1Addr == nil {
		game.Player1Addr = addr
		sendPlayerID(conn, addr, 0)
		fmt.Printf("Player 1 connected to game %d\n", game.GameID)
	} else if game.Player2Addr == nil && !addrEqual(game.Player1Addr, addr) {
		game.Player2Addr = addr
		sendPlayerID(conn, addr, 1)
		game.GameStarted = true
		game.CurrentTurn = 0
		game.LastActivity[0] = time.Now()
		game.LastActivity[1] = time.Now()
		game.Board = generateBoard()
		fmt.Printf("Player 2 connected to game %d. Game started!\n", game.GameID)
		broadcastGameState(conn, game)
	} else {
		fmt.Printf("Unable to join game %d. Game full or already started.\n", game.GameID)
	}

	fmt.Printf("Game %d state: Started=%v, Player1=%v, Player2=%v\n",
		game.GameID, game.GameStarted, game.Player1Addr, game.Player2Addr)
}

func main() {
	addr := net.UDPAddr{
		Port: PORT,
		IP:   net.ParseIP("0.0.0.0"),
	}
	conn, err := net.ListenUDP("udp", &addr)
	if err != nil {
		fmt.Println("Error listening:", err)
		return
	}
	defer conn.Close()

	fmt.Printf("Server started on port %d\n", PORT)

	go checkForDisconnects(conn)

	for {
		handleClient(conn)
	}
}

func handleClient(conn *net.UDPConn) {
	buffer := make([]byte, BUFLEN)
	n, remoteAddr, err := conn.ReadFromUDP(buffer)
	if err != nil {
		fmt.Println("Error reading from UDP:", err)
		return
	}

	message := string(buffer[:n])
	fmt.Printf("Received message from %v: %s\n", remoteAddr, message)

	if message == "CONNECT" {
		joinGame(conn, remoteAddr)
	} else if message == "DISCONNECT" {
		disconnectPlayer(conn, remoteAddr)
	} else {
		handlePlayerMove(conn, remoteAddr, message)
	}
}

func addrEqual(addr1, addr2 *net.UDPAddr) bool {
	return addr1.IP.Equal(addr2.IP) && addr1.Port == addr2.Port
}

func generateBoard() [BOARD_SIZE][BOARD_SIZE]Tile {
	var board [BOARD_SIZE][BOARD_SIZE]Tile
	for i := 0; i < BOARD_SIZE; i++ {
		for j := 0; j < BOARD_SIZE; j++ {
			board[i][j] = Tile(rand.Intn(5) + 1)
		}
	}
	return board
}

func sendPlayerID(conn *net.UDPConn, addr *net.UDPAddr, playerID int) {
	message := fmt.Sprintf("PLAYER_ID:%d", playerID)
	_, err := conn.WriteToUDP([]byte(message), addr)
	if err != nil {
		fmt.Println("Error sending player ID:", err)
	}
}

func disconnectPlayer(conn *net.UDPConn, addr *net.UDPAddr) {
	gameMutex.Lock()
	defer gameMutex.Unlock()

	for i := range games {
		if (games[i].Player1Addr != nil && addrEqual(games[i].Player1Addr, addr)) ||
			(games[i].Player2Addr != nil && addrEqual(games[i].Player2Addr, addr)) {
			games[i].GameOver = true
			broadcastGameState(conn, &games[i])
			fmt.Printf("Player disconnected from game %d. Game reset.\n", games[i].GameID)
			games[i] = GameState{}
			gameCount--
			break
		}
	}
}

func broadcastGameState(conn *net.UDPConn, game *GameState) {
	data, err := game.Serialize()
	if err != nil {
		fmt.Println("Error serializing game state:", err)
		return
	}

	if game.Player1Addr != nil {
		_, err := conn.WriteToUDP(data, game.Player1Addr)
		if err != nil {
			fmt.Printf("Error sending to player 1 (%v): %v\n", game.Player1Addr, err)
		} else {
			fmt.Printf("Sent game state to player 1 (%v)\n", game.Player1Addr)
		}
	}

	if game.Player2Addr != nil {
		_, err := conn.WriteToUDP(data, game.Player2Addr)
		if err != nil {
			fmt.Printf("Error sending to player 2 (%v): %v\n", game.Player2Addr, err)
		} else {
			fmt.Printf("Sent game state to player 2 (%v)\n", game.Player2Addr)
		}
	}
}

func checkForDisconnects(conn *net.UDPConn) {
	for {
		time.Sleep(time.Second)
		gameMutex.Lock()
		for i := range games {
			game := &games[i]
			if game.GameStarted && !game.GameOver {
				for player := 0; player < 2; player++ {
					if time.Since(game.LastActivity[player]) > GAME_TIMEOUT {
						fmt.Printf("Player %d disconnected from game %d\n", player+1, game.GameID)
						game.GameOver = true
						broadcastGameState(conn, game)
						games[i] = GameState{}
						gameCount--
						break
					}
				}
			}
		}
		gameMutex.Unlock()
	}
}

func abs(x int) int {
	if x < 0 {
		return -x
	}
	return x
}

func init() {
	rand.Seed(time.Now().UnixNano())
}

func isWithinBounds(x, y int) bool {
	return x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE
}

func removeMatches(board *[BOARD_SIZE][BOARD_SIZE]Tile, matches []Match) {
	for _, match := range matches {
		for _, point := range match.Points {
			(*board)[point.y][point.x] = Empty
		}
	}
}

func processPlayerMove(conn *net.UDPConn, game *GameState, move *PlayerMove) {
	if !game.GameStarted || game.GameOver {
		fmt.Println("Invalid move. Game not started or already over.")
		return
	}

	if move.PlayerID < 0 || move.PlayerID > 1 {
		fmt.Println("Invalid PlayerID.")
		return
	}

	if !isWithinBounds(move.FromX, move.FromY) || !isWithinBounds(move.ToX, move.ToY) {
		fmt.Println("Move coordinates out of bounds.")
		return
	}

	game.LastActivity[move.PlayerID] = time.Now()

	if int32(move.PlayerID) != game.CurrentTurn {
		fmt.Println("Invalid move. Not player's turn.")
		return
	}

	if !isValidMove(game.Board, move) {
		fmt.Println("Invalid move. Tiles not adjacent.")
		return
	}

	// swap tiles
	game.Board[move.FromY][move.FromX], game.Board[move.ToY][move.ToX] =
		game.Board[move.ToY][move.ToX], game.Board[move.FromY][move.FromX]

	fmt.Println("Tiles swapped. Checking for matches...")
	printBoard(game.Board)

	totalScore := int32(0)
	matchesFound := false

	for {
		matches := findMatches(game.Board)
		if len(matches) == 0 {
			break
		}
		matchesFound = true

		for _, match := range matches {
			matchScore := int32(len(match.Points) * 10)
			totalScore += matchScore
		}

		removeMatches(&game.Board, matches)
		for _, match := range matches {
			if len(match.Points) > MIN_MATCH {
				spawnSpecialTile(&game.Board, match)
			}
		}

		fmt.Println("Matches removed and special tiles spawned. Board after removal:")
		printBoard(game.Board)

		dropTiles(&game.Board)
		fmt.Println("Tiles dropped. Board after dropping:")
		printBoard(game.Board)

		fillEmptySpaces(&game.Board)
		fmt.Println("Empty spaces filled. Board after filling:")
		printBoard(game.Board)
	}

	if !matchesFound {
		game.Board[move.FromY][move.FromX], game.Board[move.ToY][move.ToX] =
			game.Board[move.ToY][move.ToX], game.Board[move.FromY][move.FromX]
		fmt.Println("No matches found. Move reverted.")
		broadcastGameState(conn, game)
		return
	}

	if move.PlayerID == 0 {
		game.Player1Score += totalScore
	} else {
		game.Player2Score += totalScore
	}

	game.CurrentTurn = (game.CurrentTurn + 1) % 2

	fmt.Printf("Player %d scored %d points this move.\n", move.PlayerID+1, totalScore)
	broadcastGameState(conn, game)
}

func printBoard(board [BOARD_SIZE][BOARD_SIZE]Tile) {
	for i := 0; i < BOARD_SIZE; i++ {
		for j := 0; j < BOARD_SIZE; j++ {
			fmt.Printf("%s ", tileToString(board[i][j]))
		}
		fmt.Println()
	}
}

func tileToString(t Tile) string {
	switch t {
	case Empty:
		return " "
	case Red:
		return "R"
	case Blue:
		return "B"
	case Green:
		return "G"
	case Yellow:
		return "Y"
	case Purple:
		return "P"
	case Special:
		return "S"
	default:
		return "?"
	}
}

func isValidMove(board [BOARD_SIZE][BOARD_SIZE]Tile, move *PlayerMove) bool {
	return (abs(move.FromX-move.ToX) == 1 && move.FromY == move.ToY) ||
		(abs(move.FromY-move.ToY) == 1 && move.FromX == move.ToX)
}

type Point struct {
	x, y int
}

type Match struct {
	Points    []Point
	Direction string // "horizontal" or "vertical"
}

func findMatches(board [BOARD_SIZE][BOARD_SIZE]Tile) []Match {
	var matches []Match

	// horizontal matches
	for y := 0; y < BOARD_SIZE; y++ {
		x := 0
		for x < BOARD_SIZE {
			currentTile := board[y][x]
			if currentTile == Empty {
				x++
				continue
			}
			match := []Point{{x, y}}
			k := x + 1
			for k < BOARD_SIZE && board[y][k] == currentTile {
				match = append(match, Point{k, y})
				k++
			}
			if len(match) >= MIN_MATCH {
				matches = append(matches, Match{Points: match, Direction: "horizontal"})
			}
			x = k
		}
	}

	// vertical matches
	for x := 0; x < BOARD_SIZE; x++ {
		y := 0
		for y < BOARD_SIZE {
			currentTile := board[y][x]
			if currentTile == Empty {
				y++
				continue
			}
			match := []Point{{x, y}}
			k := y + 1
			for k < BOARD_SIZE && board[k][x] == currentTile {
				match = append(match, Point{x, k})
				k++
			}
			if len(match) >= MIN_MATCH {
				matches = append(matches, Match{Points: match, Direction: "vertical"})
			}
			y = k
		}
	}

	return matches
}

func spawnSpecialTile(board *[BOARD_SIZE][BOARD_SIZE]Tile, match Match) {
	centerIndex := len(match.Points) / 2
	specialX := match.Points[centerIndex].x
	specialY := match.Points[centerIndex].y

	board[specialY][specialX] = Special
	fmt.Printf("Special tile spawned at (%d, %d)\n", specialX, specialY)
}

func dropTiles(board *[BOARD_SIZE][BOARD_SIZE]Tile) {
	for x := 0; x < BOARD_SIZE; x++ {
		emptyRow := BOARD_SIZE - 1
		for y := BOARD_SIZE - 1; y >= 0; y-- {
			if (*board)[y][x] != Empty {
				(*board)[emptyRow][x] = (*board)[y][x]
				if emptyRow != y {
					(*board)[y][x] = Empty
				}
				emptyRow--
			}
		}
	}
}

func fillEmptySpaces(board *[BOARD_SIZE][BOARD_SIZE]Tile) {
	for y := 0; y < BOARD_SIZE; y++ {
		for x := 0; x < BOARD_SIZE; x++ {
			if (*board)[y][x] == Empty {
				(*board)[y][x] = Tile(rand.Intn(5) + 1)
			}
		}
	}
}

func handlePlayerMove(conn *net.UDPConn, addr *net.UDPAddr, message string) {
	var move PlayerMove
	_, err := fmt.Sscanf(message, "%d %d %d %d %d", &move.PlayerID, &move.FromX, &move.FromY, &move.ToX, &move.ToY)
	if err != nil {
		fmt.Println("Error parsing move:", err)
		return
	}

	gameMutex.Lock()
	defer gameMutex.Unlock()

	for i := range games {
		game := &games[i]
		if (game.Player1Addr != nil && addrEqual(game.Player1Addr, addr)) ||
			(game.Player2Addr != nil && addrEqual(game.Player2Addr, addr)) {
			processPlayerMove(conn, game, &move)
			break
		}
	}
}
