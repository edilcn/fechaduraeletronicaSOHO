//Controle de operação da porta de entrada (lock0)
package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"github.com/andlabs/ui"
	_ "github.com/andlabs/ui/winmanifest"
	"net/http"
	"time"
)

var mainwin *ui.Window

const modeUrl string = "http://172.100.100.22/v2/users/SHLabs/devices/lock0/mode"
const botaoUrl string = "http://172.100.100.22/v2/users/SHLabs/devices/lock0/botao"

const token string = "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJqdGkiOiJjb250cm9sZV9nbyIsInVzciI6IlNITGFicyJ9.b8p1HrZ7lvXZRspk-ta9L8TPvJLXCJjensfy2J2dB2A"

var client = &http.Client{
	Timeout: 5 * time.Second,
}

type ModeOutput struct {
	Out int `json:"out"`
}
type ModeInput struct {
	In int `json:"in"`
}
type MomentaryInput struct {
	In bool `json:"in"`
}

func alertError(err error) {
	ui.MsgBoxError(mainwin, "Erro!", err.Error())
}

func getStatus(url string, authToken string, t interface{}) {
	req, err := http.NewRequest("GET", url, nil)
	if err != nil {
		alertError(err)
	}
	req.Header.Add("Authorization", authToken)
	req.Header.Add("Content-Type", "application/json")
	resp, err := client.Do(req)
	if err != nil {
		alertError(err)
	}

	dec := json.NewDecoder(resp.Body)
	if err := dec.Decode(&t); err != nil {
		alertError(err)
	}

	defer resp.Body.Close()
}

func postStatus(url string, authToken string, t interface{}) {
	b := new(bytes.Buffer)

	json.NewEncoder(b).Encode(t)

	req, err := http.NewRequest("POST", url, b)
	if err != nil {
		alertError(err)
	}
	req.Header.Add("Authorization", authToken)
	req.Header.Add("Content-Type", "application/json")
	resp, err := client.Do(req)
	if err != nil {
		alertError(err)
	}

	defer resp.Body.Close()
}

func makeNumbersPage() ui.Control {
	vbox := ui.NewVerticalBox()
	vbox.SetPadded(true)

	group := ui.NewGroup("Controle da Porta")
	group.SetMargined(true)
	vbox.Append(group, false)

	rb := ui.NewRadioButtons()
	rb.Append("Noturno")  //0
	rb.Append("Normal")   //1
	rb.Append("Aberto")   //2
	rb.Append("Fechado")  //3
	rb.Append("Trancado") //4
	rb.OnSelected(func(*ui.RadioButtons) {
		fmt.Println("Selected:", rb.Selected())
		postStatus(modeUrl, token, ModeInput{In: rb.Selected()})
	})
	vbox.Append(rb, true)

	vbox.Append(ui.NewHorizontalSeparator(), false)

	bt := ui.NewButton("Abrir")
	bt.OnClicked(func(*ui.Button) {
		postStatus(botaoUrl, token, MomentaryInput{In: false})
		postStatus(botaoUrl, token, MomentaryInput{In: true})
	})
	vbox.Append(bt, false)

	//ip := ui.NewProgressBar()
	//ip.SetValue(-1)

	//vbox.Append(ip, true)

	mo := new(ModeOutput)
	getStatus(modeUrl, token, &mo)
	rb.SetSelected(mo.Out)
	fmt.Println("Current:", mo.Out)

	return vbox
}

func setupUI() {
	mainwin = ui.NewWindow("Controle da Porta", 190, 230, true)
	mainwin.OnClosing(func(*ui.Window) bool {
		ui.Quit()
		return true
	})
	ui.OnShouldQuit(func() bool {
		mainwin.Destroy()
		return true
	})

	mainwin.SetChild(makeNumbersPage())
	mainwin.SetMargined(true)

	mainwin.Show()
}

func main() {
	ui.Main(setupUI)
}
